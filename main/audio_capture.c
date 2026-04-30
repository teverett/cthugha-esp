//
// Cthugha ESP32-P4 Port — I2S microphone audio capture
// Reads from an I2S MEMS microphone and fills the stereo[][] array
// with 0-255 normalized samples (matching original Cthugha format)
//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "cthugha.h"
#include "audio_capture.h"

static const char *TAG = "cthugha_audio";

static i2s_chan_handle_t rx_handle = NULL;

static int16_t raw_samples[BUFF_WIDTH * 2];

int stereo[BUFF_WIDTH][2];
int minnoise = 4;

void audio_capture_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S microphone on I2S%d", CONFIG_CTHUGHA_I2S_NUM);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        CONFIG_CTHUGHA_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_CTHUGHA_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)CONFIG_CTHUGHA_I2S_MCLK_GPIO,
            .bclk = (gpio_num_t)CONFIG_CTHUGHA_I2S_BCK_GPIO,
            .ws   = (gpio_num_t)CONFIG_CTHUGHA_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)CONFIG_CTHUGHA_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "I2S microphone ready (rate=%d)", CONFIG_CTHUGHA_I2S_SAMPLE_RATE);
}

int audio_capture_read(void)
{
    size_t bytes_read = 0;

    esp_err_t err = i2s_channel_read(rx_handle, raw_samples,
                                     BUFF_WIDTH * sizeof(int16_t),
                                     &bytes_read, pdMS_TO_TICKS(50));
    if (err != ESP_OK || bytes_read == 0)
        return 0;

    int samples_read = bytes_read / sizeof(int16_t);

    for (int i = 0; i < (int)BUFF_WIDTH; i++) {
        int16_t raw = (i < samples_read) ? raw_samples[i] : 0;

        int val = ((int)raw + 32768) >> 8;
        val = ct_clamp(val, 0, 255);

        stereo[i][0] = val;
        stereo[i][1] = val;
    }

    return 1;
}

int get_stereo_data(void)
{
    if (!audio_capture_read())
        return 0;

    int noisy = 0;
    for (int ch = 0; ch < 2; ch++) {
        int lo = 255, hi = 0;
        for (int x = 0; x < (int)BUFF_WIDTH; x++) {
            if (stereo[x][ch] > hi) hi = stereo[x][ch];
            if (stereo[x][ch] < lo) lo = stereo[x][ch];
        }
        if ((hi - lo) > minnoise)
            noisy = 1;
    }

    return noisy;
}
