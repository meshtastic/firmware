#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(ARCH_ESP32)

#include "AudioIO.h"
#include "NodeDB.h"
#include <driver/i2s_std.h>

#ifdef HAS_I2S
#include "main.h" // audioThread, which plays notification tones on the same port
#endif

// Capture and playback take a port each when the board splits the bus. The
// speaker port is the one AudioThread also wants, so it is held only while
// speaking; see audioIOEnable.
#define AUDIO_I2S_MIC_PORT I2S_NUM_0
#ifndef AUDIO_I2S_SPK_PORT
#define AUDIO_I2S_SPK_PORT I2S_NUM_1
#endif

static i2s_chan_handle_t rxChan = nullptr;
static i2s_chan_handle_t txChan = nullptr;
static size_t txFrameSamples;
static uint32_t txSampleRate;

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

static void enablePin(int pin, bool on)
{
    if (pin < 0)
        return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, on ? HIGH : LOW);
}

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples)
{
    if (rxChan || txChan)
        return true;

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRateHz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_I2S_WIDTH, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {},
    };

#if defined(AUDIO_SPLIT_BUS)
    LOG_INFO("Init I2S mic bclk %d ws %d din %d / spk bclk %d ws %d dout %d", AUDIO_MIC_BCLK, AUDIO_MIC_WS, AUDIO_MIC_DATA,
             AUDIO_SPK_BCLK, AUDIO_SPK_LRCLK, AUDIO_SPK_DATA);

    i2s_chan_config_t rxCfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_MIC_PORT, I2S_ROLE_MASTER);
    rxCfg.dma_frame_num = frameSamples;
    if (i2s_new_channel(&rxCfg, NULL, &rxChan) != ESP_OK) {
        LOG_ERROR("Audio: no I2S channel for capture");
        return false;
    }
    stdCfg.gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                       .bclk = (gpio_num_t)AUDIO_MIC_BCLK,
                       .ws = (gpio_num_t)AUDIO_MIC_WS,
                       .dout = I2S_GPIO_UNUSED,
                       .din = (gpio_num_t)AUDIO_MIC_DATA,
                       .invert_flags = {false, false, false}};
    if (i2s_channel_init_std_mode(rxChan, &stdCfg) != ESP_OK || i2s_channel_enable(rxChan) != ESP_OK) {
        LOG_ERROR("Audio: capture bus would not start");
        audioIOEnd();
        return false;
    }

    txSampleRate = sampleRateHz;
    txFrameSamples = frameSamples;
    audioIOEnable(false, false); // the module opens the speaker port per stream
    return true;

#else
    const bool wantRx = moduleConfig.audio.i2s_sd != 0;
    const bool wantTx = moduleConfig.audio.i2s_din != 0;
    if (!wantRx && !wantTx) {
        LOG_ERROR("Audio: no I2S data pins configured");
        return false;
    }

    LOG_INFO("Init I2S SD: %d DIN: %d WS: %d SCK: %d", moduleConfig.audio.i2s_sd, moduleConfig.audio.i2s_din,
             moduleConfig.audio.i2s_ws, moduleConfig.audio.i2s_sck);

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chanCfg.dma_frame_num = frameSamples;
    if (i2s_new_channel(&chanCfg, wantTx ? &txChan : NULL, wantRx ? &rxChan : NULL) != ESP_OK) {
        LOG_ERROR("Audio: could not allocate an I2S channel");
        return false;
    }

    stdCfg.gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                       .bclk = (gpio_num_t)moduleConfig.audio.i2s_sck,
                       .ws = (gpio_num_t)moduleConfig.audio.i2s_ws,
                       .dout = wantTx ? (gpio_num_t)moduleConfig.audio.i2s_din : I2S_GPIO_UNUSED,
                       .din = wantRx ? (gpio_num_t)moduleConfig.audio.i2s_sd : I2S_GPIO_UNUSED,
                       .invert_flags = {false, false, false}};

    // Same config on both channels of one port is what makes them full duplex.
    for (i2s_chan_handle_t h : {txChan, rxChan}) {
        if (!h)
            continue;
        if (i2s_channel_init_std_mode(h, &stdCfg) != ESP_OK || i2s_channel_enable(h) != ESP_OK) {
            LOG_ERROR("Audio: I2S channel would not start");
            audioIOEnd();
            return false;
        }
    }
    return true;
#endif
}

#if defined(AUDIO_SPLIT_BUS)
// Notification tones run through AudioThread on the same port, so the speaker is
// borrowed for the length of a transmission and handed straight back.
static bool openSpeaker()
{
    if (txChan)
        return true;
#ifdef HAS_I2S
    if (audioThread) // a ringtone is holding the port; live voice outranks it
        audioThread->stop();
#endif
    i2s_chan_config_t txCfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_SPK_PORT, I2S_ROLE_MASTER);
    txCfg.dma_frame_num = txFrameSamples;
    if (i2s_new_channel(&txCfg, &txChan, NULL) != ESP_OK) {
        LOG_ERROR("Audio: no I2S channel for playback");
        return false;
    }
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(txSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_I2S_WIDTH, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                     .bclk = (gpio_num_t)AUDIO_SPK_BCLK,
                     .ws = (gpio_num_t)AUDIO_SPK_LRCLK,
                     .dout = (gpio_num_t)AUDIO_SPK_DATA,
                     .din = I2S_GPIO_UNUSED,
                     .invert_flags = {false, false, false}},
    };
    if (i2s_channel_init_std_mode(txChan, &cfg) != ESP_OK || i2s_channel_enable(txChan) != ESP_OK) {
        LOG_ERROR("Audio: playback bus would not start");
        i2s_del_channel(txChan);
        txChan = nullptr;
        return false;
    }
    return true;
}

static void closeSpeaker()
{
    if (!txChan)
        return;
    i2s_channel_disable(txChan);
    i2s_del_channel(txChan);
    txChan = nullptr;
}
#endif

void audioIOEnable(bool capture, bool playback)
{
#if defined(AUDIO_SPLIT_BUS)
    if (playback)
        openSpeaker();
    else
        closeSpeaker();
#endif
#ifdef AUDIO_MIC_EN
    enablePin(AUDIO_MIC_EN, capture);
#else
    (void)capture;
#endif
#ifdef AUDIO_SPK_SD
    enablePin(AUDIO_SPK_SD, playback); // MAX98357 SD_MODE
#else
    (void)playback;
#endif
}

void audioIOEnd()
{
    audioIOEnable(false, false);
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
