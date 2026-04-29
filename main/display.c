//
// Cthugha ESP32-P4 Port — MIPI-DSI LCD display driver
// Scales the 240x240 8-bit indexed buffer to 720x720 RGB565 via 3x nearest-neighbor
//

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "driver/gpio.h"
#include "cthugha.h"
#include "display.h"

static const char *TAG = "cthugha_display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *lcd_fb[2] = {NULL, NULL};
static int cur_fb = 0;

// Pre-computed palette LUT: indexed color → RGB565
static uint16_t pal_lut[256];

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) |
           ((uint16_t)(g >> 2) << 5)  |
           ((uint16_t)(b >> 3));
}

static void update_palette_lut(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t r = LUTbuffer[i * 3 + 0];
        uint8_t g = LUTbuffer[i * 3 + 1];
        uint8_t b = LUTbuffer[i * 3 + 2];
        pal_lut[i] = rgb888_to_rgb565(r, g, b);
    }
}

// Display effect function pointers (ported from DISPLAY.C)
void (*display_effect)(void);
int numdisplays = -1;
int curdisplay = 0;

static void display_upwards(void)
{
    // Direct: buff maps straight to output
    // (no transform, identity mapping)
}

static void display_downwards(void)
{
    for (unsigned int y = 0; y < BUFF_HEIGHT; y++)
        memcpy(shadow + y * BUFF_WIDTH,
               buff + (BUFF_HEIGHT - 1 - y) * BUFF_WIDTH, BUFF_WIDTH);
    flip_screens();
}

static void display_hor_split_out(void)
{
    unsigned int mid = BUFF_HEIGHT / 2;
    for (unsigned int y = 0; y < mid; y++)
        memcpy(shadow + y * BUFF_WIDTH,
               buff + (mid - 1 - y + mid) * BUFF_WIDTH, BUFF_WIDTH);
    memcpy(shadow + mid * BUFF_WIDTH,
           buff + mid * BUFF_WIDTH, mid * BUFF_WIDTH);
    flip_screens();
}

static void display_hor_split_in(void)
{
    unsigned int mid = BUFF_HEIGHT / 2;
    memcpy(shadow, buff + mid * BUFF_WIDTH, mid * BUFF_WIDTH);
    for (unsigned int y = 0; y < mid; y++)
        memcpy(shadow + (mid + y) * BUFF_WIDTH,
               buff + (mid + mid - 1 - y) * BUFF_WIDTH, BUFF_WIDTH);
    flip_screens();
}

static void display_kaleidoscope(void)
{
    unsigned int mid_x = BUFF_WIDTH / 2;
    unsigned int mid_y = BUFF_HEIGHT / 2;
    for (unsigned int y = 0; y < mid_y; y++) {
        memcpy(shadow + y * BUFF_WIDTH, buff + (y + mid_y) * BUFF_WIDTH, mid_x);
        for (unsigned int x = 0; x < mid_x; x++)
            shadow[y * BUFF_WIDTH + mid_x + x] = buff[(y + mid_y) * BUFF_WIDTH + mid_x - 1 - x];
    }
    for (unsigned int y = 0; y < mid_y; y++)
        memcpy(shadow + (mid_y + y) * BUFF_WIDTH,
               shadow + (mid_y - 1 - y) * BUFF_WIDTH, BUFF_WIDTH);
    flip_screens();
}

static void display_rot90_mirror(void)
{
    for (unsigned int y = 0; y < BUFF_HEIGHT; y++) {
        for (unsigned int x = 0; x < BUFF_WIDTH / 2; x++)
            shadow[y * BUFF_WIDTH + x] = buff[x * BUFF_WIDTH + y];
        for (unsigned int x = BUFF_WIDTH / 2; x < BUFF_WIDTH; x++)
            shadow[y * BUFF_WIDTH + x] = buff[(BUFF_WIDTH - 1 - x) * BUFF_WIDTH + y];
    }
    flip_screens();
}

static void display_rot90_mirror2(void)
{
    for (unsigned int y = 0; y < BUFF_HEIGHT; y++) {
        unsigned int src_col = BUFF_HEIGHT - 1 - y;
        for (unsigned int x = 0; x < BUFF_WIDTH / 2; x++)
            shadow[y * BUFF_WIDTH + x] = buff[x * BUFF_WIDTH + src_col];
        for (unsigned int x = BUFF_WIDTH / 2; x < BUFF_WIDTH; x++)
            shadow[y * BUFF_WIDTH + x] = buff[(BUFF_WIDTH - 1 - x) * BUFF_WIDTH + src_col];
    }
    flip_screens();
}

static void display_rot90_kaleidoscope(void)
{
    unsigned int mid_x = BUFF_WIDTH / 2;
    unsigned int mid_y = BUFF_HEIGHT / 2;
    for (unsigned int y = 0; y < mid_y; y++) {
        for (unsigned int x = 0; x < mid_x; x++) {
            uint8_t pixel = buff[x * BUFF_WIDTH + y];
            shadow[y * BUFF_WIDTH + x] = pixel;
            shadow[(BUFF_HEIGHT - 1 - y) * BUFF_WIDTH + x] = pixel;
        }
        for (unsigned int x = mid_x; x < BUFF_WIDTH; x++) {
            uint8_t pixel = buff[(BUFF_WIDTH - 1 - x) * BUFF_WIDTH + y];
            shadow[y * BUFF_WIDTH + x] = pixel;
            shadow[(BUFF_HEIGHT - 1 - y) * BUFF_WIDTH + x] = pixel;
        }
    }
    flip_screens();
}

function_opt disparray[] = {
    { display_upwards,           WHEN_ALWAYS, "Upwards"             },
    { display_downwards,         WHEN_ALWAYS, "Downwards"           },
    { display_hor_split_out,     WHEN_ALWAYS, "Hor. Split out"      },
    { display_hor_split_in,      WHEN_ALWAYS, "Hor. Split in"       },
    { display_kaleidoscope,      WHEN_ALWAYS, "Kaleidoscope"        },
    { display_rot90_mirror,      WHEN_ALWAYS, "90deg rot. mirror"   },
    { display_rot90_mirror2,     WHEN_ALWAYS, "90deg rot. mirror 2" },
    { display_rot90_kaleidoscope,WHEN_ALWAYS, "90deg Kaleidoscope"  },
    { NULL,                      WHEN_NEVER,  "<END>"               }
};

int change_display(int disp)
{
    if (numdisplays < 0) {
        numdisplays = 0;
        while (disparray[numdisplays].function != NULL)
            numdisplays++;
    }
    if (numdisplays == 0) return 0;

    disp = ((disp % numdisplays) + numdisplays) % numdisplays;
    display_effect = disparray[disp].function;
    return disp;
}

void flip_screens(void)
{
    uint8_t *temp = buff;
    buff = shadow;
    shadow = temp;
}

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing MIPI-DSI display %dx%d", LCD_H_RES, LCD_V_RES);

    // Backlight
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_CTHUGHA_LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(CONFIG_CTHUGHA_LCD_BL_GPIO, 1);

    // MIPI-DSI bus
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = CONFIG_CTHUGHA_LCD_DSI_NUM_DATA_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = CONFIG_CTHUGHA_LCD_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));

    // DBI panel IO for sending init commands to the LCD controller IC
    esp_lcd_panel_io_handle_t dbi_io;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io));

    // DPI panel for continuous pixel output
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = CONFIG_CTHUGHA_LCD_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_back_porch  = CONFIG_CTHUGHA_LCD_HSYNC_BACK_PORCH,
            .hsync_pulse_width = CONFIG_CTHUGHA_LCD_HSYNC_PULSE_WIDTH,
            .hsync_front_porch = CONFIG_CTHUGHA_LCD_HSYNC_FRONT_PORCH,
            .vsync_back_porch  = CONFIG_CTHUGHA_LCD_VSYNC_BACK_PORCH,
            .vsync_pulse_width = CONFIG_CTHUGHA_LCD_VSYNC_PULSE_WIDTH,
            .vsync_front_porch = CONFIG_CTHUGHA_LCD_VSYNC_FRONT_PORCH,
        },
        .flags = {
            .use_dma2d = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &panel_handle));

    // TODO: Send LCD controller IC init commands via dbi_io here.
    // The exact sequence depends on the panel IC (JD9365, ST7701S, etc.)
    // on the specific Waveshare ESP32-P4-WIFI6-Touch-LCD-4B board.
    // Check the board schematic and panel datasheet.

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Get the DPI framebuffer pointers
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel_handle, 2,
                    (void **)&lcd_fb[0], (void **)&lcd_fb[1]));

    // Clear both framebuffers
    memset(lcd_fb[0], 0, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    memset(lcd_fb[1], 0, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));

    ESP_LOGI(TAG, "Display initialized. FB0=%p FB1=%p", lcd_fb[0], lcd_fb[1]);
}

void display_render(void)
{
    update_palette_lut();

    uint16_t *fb = lcd_fb[cur_fb];

    // Scale 240x240 → 720x720 via 3x nearest-neighbor with palette lookup
    for (int sy = 0; sy < (int)BUFF_HEIGHT; sy++) {
        int dy_base = sy * SCALE_FACTOR;
        const uint8_t *src_row = buff + sy * BUFF_WIDTH;

        // Build one scaled row
        uint16_t *dst_row0 = fb + dy_base * LCD_H_RES;
        for (int sx = 0; sx < (int)BUFF_WIDTH; sx++) {
            uint16_t color = pal_lut[src_row[sx]];
            int dx_base = sx * SCALE_FACTOR;
            for (int rx = 0; rx < SCALE_FACTOR; rx++)
                dst_row0[dx_base + rx] = color;
        }

        // Replicate to remaining rows in the scale block
        for (int ry = 1; ry < SCALE_FACTOR; ry++)
            memcpy(fb + (dy_base + ry) * LCD_H_RES,
                   dst_row0, LCD_H_RES * sizeof(uint16_t));
    }

    // Swap framebuffer for next frame
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, fb);
    cur_fb = 1 - cur_fb;
}
