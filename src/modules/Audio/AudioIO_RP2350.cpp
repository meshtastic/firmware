#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(__PLAT_RP2350__)

#include "AudioIO.h"
#include "NodeDB.h"
#include "mesh/Throttle.h"
#include <I2S.h>

// The PIO drives BCLK and the word clock from one side-set pair, so LRCLK is
// always the pin after BCLK. Any other wiring clocks the wrong GPIO.
#define AUDIO_LRCLK_OF(bclk) ((bclk) + 1)

// Buffers of one codec2 frame each. Playback needs the depth: a decode can take
// longer than the frame it produced, and an underrun is an audible click.
#define AUDIO_I2S_BUFFERS 4

#if defined(AUDIO_SPLIT_BUS)
static_assert(AUDIO_MIC_WS == AUDIO_LRCLK_OF(AUDIO_MIC_BCLK), "RP2350 I2S: AUDIO_MIC_WS must be AUDIO_MIC_BCLK + 1");
static_assert(AUDIO_SPK_LRCLK == AUDIO_LRCLK_OF(AUDIO_SPK_BCLK), "RP2350 I2S: AUDIO_SPK_LRCLK must be AUDIO_SPK_BCLK + 1");

static I2S micI2S(INPUT);
static I2S spkI2S(OUTPUT);
#else
// One bus both ways: INPUT_PULLUP selects the pio_i2s_inout program, the only
// one that carries DIN and DOUT on a single clock pair.
static I2S busI2S(INPUT_PULLUP);
static I2S &micI2S = busI2S;
static I2S &spkI2S = busI2S;
#endif

// Playback is 16 bit; only the microphone follows AUDIO_I2S_SLOT_BITS, and on a
// shared bus its width applies to both.
#if AUDIO_I2S_SLOT_BITS == 32
#define AUDIO_RX_WORDS 2
#else
#define AUDIO_RX_WORDS 1
#endif
#if defined(AUDIO_SPLIT_BUS) || AUDIO_I2S_SLOT_BITS == 16
#define AUDIO_TX_WORDS 1
#else
#define AUDIO_TX_WORDS 2
#endif

static bool running;

static void enablePin(int pin, bool on)
{
    if (pin < 0)
        return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, on ? HIGH : LOW);
}

void audioIOEnable(bool capture, bool playback)
{
#ifdef AUDIO_MIC_EN
    enablePin(AUDIO_MIC_EN, capture);
#else
    (void)capture;
#endif
#ifdef AUDIO_SPK_SD
    enablePin(AUDIO_SPK_SD, playback);
#else
    (void)playback;
#endif
}

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples)
{
    if (running)
        return true;

#if defined(AUDIO_SPLIT_BUS)
    LOG_INFO("Init I2S mic bclk %d din %d / spk bclk %d dout %d", AUDIO_MIC_BCLK, AUDIO_MIC_DATA, AUDIO_SPK_BCLK, AUDIO_SPK_DATA);

    micI2S.setBCLK(AUDIO_MIC_BCLK);
    micI2S.setDIN(AUDIO_MIC_DATA);
    micI2S.setBitsPerSample(AUDIO_I2S_SLOT_BITS);
    micI2S.setFrequency(sampleRateHz);
    micI2S.setBuffers(AUDIO_I2S_BUFFERS, frameSamples);

    spkI2S.setBCLK(AUDIO_SPK_BCLK);
    spkI2S.setDOUT(AUDIO_SPK_DATA);
    spkI2S.setBitsPerSample(16);
    spkI2S.setFrequency(sampleRateHz);
    spkI2S.setBuffers(AUDIO_I2S_BUFFERS, frameSamples);

    if (!micI2S.begin()) {
        LOG_ERROR("Audio: I2S capture would not start");
        return false;
    }
    if (!spkI2S.begin()) {
        LOG_ERROR("Audio: I2S playback would not start");
        micI2S.end();
        return false;
    }
#else
    const int bclk = moduleConfig.audio.i2s_sck;
    const int din = moduleConfig.audio.i2s_sd;   // microphone data in
    const int dout = moduleConfig.audio.i2s_din; // amplifier data out
    if (!bclk || (!din && !dout)) {
        LOG_ERROR("Audio: set moduleConfig.audio i2s_sck plus i2s_sd and/or i2s_din");
        return false;
    }
    if (moduleConfig.audio.i2s_ws != AUDIO_LRCLK_OF(bclk)) {
        LOG_ERROR("Audio: RP2350 I2S needs i2s_ws == i2s_sck + 1, got ws %d sck %d", moduleConfig.audio.i2s_ws, bclk);
        return false;
    }

    LOG_INFO("Init I2S bclk %d ws %d din %d dout %d", bclk, moduleConfig.audio.i2s_ws, din, dout);
    busI2S.setBCLK(bclk);
    if (din)
        busI2S.setDIN(din);
    if (dout)
        busI2S.setDOUT(dout);
    busI2S.setBitsPerSample(AUDIO_I2S_SLOT_BITS);
    busI2S.setFrequency(sampleRateHz);
    busI2S.setBuffers(AUDIO_I2S_BUFFERS, frameSamples);
    if (!busI2S.begin()) {
        LOG_ERROR("Audio: I2S would not start");
        return false;
    }
#endif

    running = true;
    audioIOEnable(false, false); // the module powers these per PTT edge
    return true;
}

void audioIOEnd()
{
    audioIOEnable(false, false);
    if (!running)
        return;
    micI2S.end();
#if defined(AUDIO_SPLIT_BUS)
    spkI2S.end();
#endif
    running = false;
}

size_t audioIORead(int16_t *dst, size_t samples, uint32_t timeoutMs)
{
    if (!running)
        return 0;

    const uint32_t start = millis();
    size_t out = 0;

    while (out < samples && Throttle::isWithinTimespanMs(start, timeoutMs)) {
        // read16/read32 spin until the DMA delivers, so only call them once the
        // samples are already there and the timeout stays honest.
        if (micI2S.available() < (int)(AUDIO_RX_WORDS * sizeof(uint32_t))) {
            yield();
            continue;
        }
#if AUDIO_I2S_SLOT_BITS == 32
        int32_t l = 0, r = 0;
        if (!micI2S.read32(&l, &r))
            break;
        dst[out++] = (int16_t)(l >> 16); // MSB aligned in the wide slot
#else
        int16_t l = 0, r = 0;
        if (!micI2S.read16(&l, &r))
            break;
        dst[out++] = l;
#endif
        (void)r; // mono: the mic drives one channel, the other is its pair
    }
    return out;
}

size_t audioIOWrite(const int16_t *src, size_t samples, uint32_t timeoutMs)
{
    if (!running)
        return 0;

    const uint32_t start = millis();
    size_t out = 0;

    while (out < samples && Throttle::isWithinTimespanMs(start, timeoutMs)) {
        if (spkI2S.availableForWrite() < (int)(AUDIO_TX_WORDS * sizeof(uint32_t))) {
            yield();
            continue;
        }
        // Same sample both channels, so the amplifier's channel strap does not
        // decide whether anything is heard.
#if AUDIO_TX_WORDS == 2
        const int32_t s = (int32_t)src[out] << 16;
        if (!spkI2S.write32(s, s))
            break;
#else
        if (!spkI2S.write16(src[out], src[out]))
            break;
#endif
        out++;
    }
    return out;
}

#endif
