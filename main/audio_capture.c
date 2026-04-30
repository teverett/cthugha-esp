//
// Cthugha ESP32-P4 Port — I2S audio capture via ES8311 codec
// ES8311 initialized directly over the shared I2C master bus (new API).
// I2S runs full-duplex stereo (TX+RX); L channel carries the ADC mic data.
//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "cthugha.h"
#include "audio_capture.h"
#include "touch_input.h"

static const char *TAG = "cthugha_audio";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static i2c_master_dev_handle_t es8311_dev = NULL;

// Stereo interleaved [L0,R0,L1,R1,...] — 240 pairs = 480 int16 = 960 bytes
static int16_t raw_samples[BUFF_WIDTH * 2];

int stereo[BUFF_WIDTH][2];
int minnoise = 4;

// --- ES8311 codec via new I2C master API ---
// Address: CE pin low = 0x18, CE pin high = 0x19
#define ES8311_ADDR 0x18

static esp_err_t es_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(es8311_dev, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t es_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(es8311_dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

// Read-modify-write: keep bits under keep_mask, OR in or_val
static esp_err_t es_rmw(uint8_t reg, uint8_t keep_mask, uint8_t or_val)
{
    uint8_t cur = 0;
    esp_err_t err = es_read(reg, &cur);
    if (err != ESP_OK) return err;
    return es_write(reg, (cur & keep_mask) | or_val);
}

static void es8311_codec_init(void)
{
    i2c_master_bus_handle_t bus = touch_get_i2c_bus();

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8311_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &es8311_dev));

    // Reset and power on
    ESP_ERROR_CHECK(es_write(0x00, 0x1F));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(es_write(0x00, 0x00));
    ESP_ERROR_CHECK(es_write(0x00, 0x80));

    // Enable all clocks; MCLK sourced from MCLK pin
    ESP_ERROR_CHECK(es_write(0x01, 0x3F));

    // Clock dividers for MCLK=6144000 Hz, rate=16000 Hz
    // coeff_div entry: pre_div=3, pre_multi=1(2x), adc_div=1, dac_div=1,
    //                  lrck_h=0x00, lrck_l=0xFF, bclk_div=4, osr=0x10
    ESP_ERROR_CHECK(es_rmw(0x02, 0x07, 0x48)); // (pre_div-1)<<5 | pre_multi<<3
    ESP_ERROR_CHECK(es_write(0x03, 0x10));      // ADC OSR
    ESP_ERROR_CHECK(es_write(0x04, 0x10));      // DAC OSR
    ESP_ERROR_CHECK(es_write(0x05, 0x00));      // ADC/DAC clock dividers = 1
    // REG06: clear SCLK-inv (bit5), set BCLK div=4 (bits[4:0]=3)
    ESP_ERROR_CHECK(es_rmw(0x06, 0xC0, 0x03));
    // REG07: clear tri-state (bit7), set LRCK_H=0 — use mask 0x40 to explicitly clear bit7
    ESP_ERROR_CHECK(es_rmw(0x07, 0x40, 0x00));
    ESP_ERROR_CHECK(es_write(0x08, 0xFF));      // LRCK_L = 255

    // Serial port: I2S slave, 16-bit resolution
    ESP_ERROR_CHECK(es_rmw(0x00, 0xBF, 0x00)); // slave (keep bit7, clear bit6)
    ESP_ERROR_CHECK(es_write(0x09, 0x0C));      // SDP In (DAC): 16-bit I2S
    ESP_ERROR_CHECK(es_write(0x0A, 0x0C));      // SDP Out (ADC): 16-bit I2S

    // Power up analog circuitry and ADC
    ESP_ERROR_CHECK(es_write(0x0D, 0x01)); // power up analog
    ESP_ERROR_CHECK(es_write(0x0E, 0x02)); // enable PGA + ADC modulator
    ESP_ERROR_CHECK(es_write(0x12, 0x00)); // power up DAC
    ESP_ERROR_CHECK(es_write(0x13, 0x10)); // enable headphone output
    ESP_ERROR_CHECK(es_write(0x1C, 0x6A)); // ADC equalizer bypass
    ESP_ERROR_CHECK(es_write(0x37, 0x08)); // bypass DAC equalizer

    // Microphone: analog mic, max PGA + 42 dB preamp gain
    ESP_ERROR_CHECK(es_write(0x17, 0xC8)); // ADC digital gain
    ESP_ERROR_CHECK(es_write(0x14, 0x1A)); // analog MIC, max PGA
    ESP_ERROR_CHECK(es_write(0x16, 0x07)); // mic preamp gain: 42 dB

    // Read back key registers to verify writes took effect
    uint8_t r00, r01, r07, r09, r0a, r0e, r14;
    es_read(0x00, &r00); es_read(0x01, &r01); es_read(0x07, &r07);
    es_read(0x09, &r09); es_read(0x0A, &r0a);
    es_read(0x0E, &r0e); es_read(0x14, &r14);
    ESP_LOGI(TAG, "ES8311 readback: R00=%02X R01=%02X R07=%02X R09=%02X R0A=%02X R0E=%02X R14=%02X",
             r00, r01, r07, r09, r0a, r0e, r14);
    ESP_LOGI(TAG, "ES8311 codec initialized at I2C addr 0x%02X", ES8311_ADDR);
}

void audio_capture_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S+ES8311 on I2S%d", CONFIG_CTHUGHA_I2S_NUM);

    // Open duplex (TX+RX) — matches demo config; ensures BCLK/WS are driven
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        CONFIG_CTHUGHA_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_CTHUGHA_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)CONFIG_CTHUGHA_I2S_MCLK_GPIO,
            .bclk = (gpio_num_t)CONFIG_CTHUGHA_I2S_BCK_GPIO,
            .ws   = (gpio_num_t)CONFIG_CTHUGHA_I2S_WS_GPIO,
            .dout = (gpio_num_t)CONFIG_CTHUGHA_I2S_DOUT_GPIO,
            .din  = (gpio_num_t)CONFIG_CTHUGHA_I2S_DIN_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    // Codec initialized after I2S clocks are running (ES8311 needs MCLK)
    es8311_codec_init();

    ESP_LOGI(TAG, "Audio ready (rate=%d stereo)", CONFIG_CTHUGHA_I2S_SAMPLE_RATE);
}

int audio_capture_read(void)
{
    size_t bytes_read = 0;
    // 240 stereo pairs × 4 bytes = 960 bytes
    esp_err_t err = i2s_channel_read(rx_handle, raw_samples,
                                     BUFF_WIDTH * 2 * sizeof(int16_t),
                                     &bytes_read, pdMS_TO_TICKS(50));

    static int dbg = 0;
    if (++dbg >= 300) {
        dbg = 0;
        int n = (int)(bytes_read / sizeof(int16_t));
        ESP_LOGI(TAG, "I2S L[0]=%d R[0]=%d L[60]=%d R[60]=%d L[120]=%d R[120]=%d (err=%d bytes=%d)",
                 n > 0   ? raw_samples[0]   : -1,
                 n > 1   ? raw_samples[1]   : -1,
                 n > 120 ? raw_samples[120] : -1,
                 n > 121 ? raw_samples[121] : -1,
                 n > 240 ? raw_samples[240] : -1,
                 n > 241 ? raw_samples[241] : -1,
                 (int)err, (int)bytes_read);
    }

    if (err != ESP_OK || bytes_read == 0)
        return 0;

    // Stereo interleaved: even indices = L channel (ADC mic data)
    int pairs = (int)(bytes_read / (2 * sizeof(int16_t)));
    for (int i = 0; i < (int)BUFF_WIDTH; i++) {
        int16_t raw = (i < pairs) ? raw_samples[i * 2] : 0;
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
