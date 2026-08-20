#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(ARCH_ESP32)

#include "AudioIO.h"
#include "NodeDB.h"
#include <driver/i2s.h>

#define AUDIO_I2S_PORT I2S_NUM_0

static bool started = false;

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples)
{
    if (started)
        return true;

    LOG_INFO("Init I2S SD: %d DIN: %d WS: %d SCK: %d", moduleConfig.audio.i2s_sd, moduleConfig.audio.i2s_din,
             moduleConfig.audio.i2s_ws, moduleConfig.audio.i2s_sck);

    i2s_config_t cfg = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | (moduleConfig.audio.i2s_sd ? I2S_MODE_RX : 0) |
                                             (moduleConfig.audio.i2s_din ? I2S_MODE_TX : 0)),
                        .sample_rate = sampleRateHz,
                        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
                        .intr_alloc_flags = 0,
                        .dma_buf_count = 8,
                        .dma_buf_len = (int)frameSamples,
                        .use_apll = false,
                        .tx_desc_auto_clear = true,
                        .fixed_mclk = 0};

    esp_err_t res = i2s_driver_install(AUDIO_I2S_PORT, &cfg, 0, NULL);
    if (res != ESP_OK) {
        LOG_ERROR("Failed to install I2S driver: %d", res);
        return false;
    }

    const i2s_pin_config_t pins = {.bck_io_num = moduleConfig.audio.i2s_sck,
                                   .ws_io_num = moduleConfig.audio.i2s_ws,
                                   .data_out_num = moduleConfig.audio.i2s_din ? moduleConfig.audio.i2s_din : I2S_PIN_NO_CHANGE,
                                   .data_in_num = moduleConfig.audio.i2s_sd ? moduleConfig.audio.i2s_sd : I2S_PIN_NO_CHANGE};
    res = i2s_set_pin(AUDIO_I2S_PORT, &pins);
    if (res != ESP_OK) {
        LOG_ERROR("Failed to set I2S pin config: %d", res);
        i2s_driver_uninstall(AUDIO_I2S_PORT);
        return false;
    }

    res = i2s_start(AUDIO_I2S_PORT);
    if (res != ESP_OK) {
        LOG_ERROR("Failed to start I2S: %d", res);
        i2s_driver_uninstall(AUDIO_I2S_PORT);
        return false;
    }

    started = true;
    return true;
}

void audioIOEnd()
{
    if (!started)
        return;
    i2s_stop(AUDIO_I2S_PORT);
    i2s_driver_uninstall(AUDIO_I2S_PORT);
    started = false;
}

size_t audioIORead(int16_t *dst, size_t samples, uint32_t timeoutMs)
{
    if (!started)
        return 0;
    size_t bytesIn = 0;
    if (i2s_read(AUDIO_I2S_PORT, dst, samples * sizeof(int16_t), &bytesIn, pdMS_TO_TICKS(timeoutMs)) != ESP_OK)
        return 0;
    return bytesIn / sizeof(int16_t);
}

size_t audioIOWrite(const int16_t *src, size_t samples, uint32_t timeoutMs)
{
    if (!started)
        return 0;
    size_t bytesOut = 0;
    if (i2s_write(AUDIO_I2S_PORT, src, samples * sizeof(int16_t), &bytesOut, pdMS_TO_TICKS(timeoutMs)) != ESP_OK)
        return 0;
    return bytesOut / sizeof(int16_t);
}

#endif
