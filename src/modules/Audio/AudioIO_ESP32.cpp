#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(ARCH_ESP32)

#include "AudioIO.h"
#include "NodeDB.h"
#include <driver/i2s_std.h>

#define AUDIO_I2S_PORT I2S_NUM_0

static i2s_chan_handle_t rxChan = nullptr;
static i2s_chan_handle_t txChan = nullptr;

#if AUDIO_I2S_SLOT_BITS == 32
// Wide slots carry the sample MSB-aligned, so the codec's int16 has to be
// unpacked from / packed into 32-bit words. Separate buffers: capture and
// playback still run on different threads until the codec task goes away.
static int32_t rxScratch[AUDIO_FRAME_SAMPLES_MAX];
static int32_t txScratch[AUDIO_FRAME_SAMPLES_MAX];
#define AUDIO_I2S_WIDTH I2S_DATA_BIT_WIDTH_32BIT
#else
#define AUDIO_I2S_WIDTH I2S_DATA_BIT_WIDTH_16BIT
#endif

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples)
{
    if (rxChan || txChan)
        return true;

    const bool wantRx = moduleConfig.audio.i2s_sd != 0;
    const bool wantTx = moduleConfig.audio.i2s_din != 0;
    if (!wantRx && !wantTx) {
        LOG_ERROR("Audio: no I2S data pins configured");
        return false;
    }

    LOG_INFO("Init I2S SD: %d DIN: %d WS: %d SCK: %d", moduleConfig.audio.i2s_sd, moduleConfig.audio.i2s_din,
             moduleConfig.audio.i2s_ws, moduleConfig.audio.i2s_sck);

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chanCfg.dma_frame_num = frameSamples;

    esp_err_t res = i2s_new_channel(&chanCfg, wantTx ? &txChan : nullptr, wantRx ? &rxChan : nullptr);
    if (res != ESP_OK) {
        LOG_ERROR("Failed to allocate I2S channel: %d", res);
        return false;
    }

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRateHz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_I2S_WIDTH, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                     .bclk = (gpio_num_t)moduleConfig.audio.i2s_sck,
                     .ws = (gpio_num_t)moduleConfig.audio.i2s_ws,
                     .dout = wantTx ? (gpio_num_t)moduleConfig.audio.i2s_din : I2S_GPIO_UNUSED,
                     .din = wantRx ? (gpio_num_t)moduleConfig.audio.i2s_sd : I2S_GPIO_UNUSED,
                     .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
    };

    // Both directions share the port, so identical configs make them full duplex.
    for (i2s_chan_handle_t h : {txChan, rxChan}) {
        if (!h)
            continue;
        res = i2s_channel_init_std_mode(h, &stdCfg);
        if (res == ESP_OK)
            res = i2s_channel_enable(h);
        if (res != ESP_OK) {
            LOG_ERROR("Failed to start I2S channel: %d", res);
            audioIOEnd();
            return false;
        }
    }

    return true;
}

void audioIOEnd()
{
    for (i2s_chan_handle_t *h : {&txChan, &rxChan}) {
        if (!*h)
            continue;
        i2s_channel_disable(*h);
        i2s_del_channel(*h);
        *h = nullptr;
    }
}

size_t audioIORead(int16_t *dst, size_t samples, uint32_t timeoutMs)
{
    if (!rxChan)
        return 0;
    size_t bytesIn = 0;

#if AUDIO_I2S_SLOT_BITS == 32
    if (samples > AUDIO_FRAME_SAMPLES_MAX)
        samples = AUDIO_FRAME_SAMPLES_MAX;
    if (i2s_channel_read(rxChan, rxScratch, samples * sizeof(int32_t), &bytesIn, timeoutMs) != ESP_OK)
        return 0;
    const size_t got = bytesIn / sizeof(int32_t);
    for (size_t i = 0; i < got; i++)
        dst[i] = (int16_t)(rxScratch[i] >> 16);
    return got;
#else
    if (i2s_channel_read(rxChan, dst, samples * sizeof(int16_t), &bytesIn, timeoutMs) != ESP_OK)
        return 0;
    return bytesIn / sizeof(int16_t);
#endif
}

size_t audioIOWrite(const int16_t *src, size_t samples, uint32_t timeoutMs)
{
    if (!txChan)
        return 0;
    size_t bytesOut = 0;

#if AUDIO_I2S_SLOT_BITS == 32
    if (samples > AUDIO_FRAME_SAMPLES_MAX)
        samples = AUDIO_FRAME_SAMPLES_MAX;
    for (size_t i = 0; i < samples; i++)
        txScratch[i] = (int32_t)src[i] << 16;
    if (i2s_channel_write(txChan, txScratch, samples * sizeof(int32_t), &bytesOut, timeoutMs) != ESP_OK)
        return 0;
    return bytesOut / sizeof(int32_t);
#else
    if (i2s_channel_write(txChan, src, samples * sizeof(int16_t), &bytesOut, timeoutMs) != ESP_OK)
        return 0;
    return bytesOut / sizeof(int16_t);
#endif
}

#endif
