#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(ARCH_NRF52)

#include "AudioIO.h"
#include "mesh/Throttle.h"
#include <PDM.h>
#include <nrf_i2s.h>

#if !defined(MIC_DATA) || !defined(MIC_SCLK)
#error "HAS_AUDIO_CODEC2 on nRF52 needs MIC_DATA and MIC_SCLK in variant.h"
#endif
#if !defined(SPEAKER_BCLK) || !defined(SPEAKER_WS_LRCK) || !defined(SPEAKER_DATA)
#error "HAS_AUDIO_CODEC2 on nRF52 needs SPEAKER_BCLK, SPEAKER_WS_LRCK and SPEAKER_DATA in variant.h"
#endif

// The PDM peripheral cannot clock below 16 kHz (PDM.cpp offers 16000 or 41667
// only), so capture runs at 16 kHz and decimates 2:1. Playback is exact:
// 32 MHz / 125 / 32 = 8000 Hz.
#define PDM_CAPTURE_HZ 16000

// 11-tap half-band low-pass, cutoff Fs/4, Q15. Without it the 4-8 kHz band
// folds straight back onto the speech when decimating. -1.9 dB at 3 kHz,
// -28.7 dB at 6 kHz, -43.8 dB at 8 kHz. Alternate taps are zero by
// construction, so it costs six multiplies per output sample.
static constexpr int FIR_TAPS = 11;
static constexpr int16_t FIR[FIR_TAPS] = {166, 0, -1374, 0, 9453, 16278, 9453, 0, -1374, 0, 166};

static int16_t firHist[FIR_TAPS];
static bool firPhase;

static bool captureOn;
static bool playbackOn;
static bool txRunning; // EasyDMA started - the HAL has no enable-check

// Stereo frames, same sample in both halves so the amp's channel strap does not
// matter. Two buffers so EasyDMA always has somewhere to go.
static uint32_t txBuf[2][AUDIO_FRAME_SAMPLES_MAX];
static uint8_t txIdx;

static inline int16_t firStep(int16_t in)
{
    for (int i = 0; i < FIR_TAPS - 1; i++)
        firHist[i] = firHist[i + 1];
    firHist[FIR_TAPS - 1] = in;

    int32_t acc = 0;
    for (int i = 0; i < FIR_TAPS; i++)
        acc += (int32_t)FIR[i] * firHist[i];
    return (int16_t)(acc >> 15);
}

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples)
{
    (void)frameSamples;
    if (sampleRateHz != 8000) {
        LOG_ERROR("Audio: nRF52 backend is 8 kHz only, asked for %u", sampleRateHz);
        return false;
    }
    if (captureOn || playbackOn)
        return true;

    memset(firHist, 0, sizeof(firHist));
    firPhase = false;

    PDM.setPins(MIC_DATA, MIC_SCLK, -1);
    if (!PDM.begin(1, PDM_CAPTURE_HZ)) {
        LOG_ERROR("Audio: PDM begin failed");
        return false;
    }
    captureOn = true;

    nrf_i2s_pins_set(NRF_I2S, SPEAKER_BCLK, SPEAKER_WS_LRCK, NRF_I2S_PIN_NOT_CONNECTED, SPEAKER_DATA, NRF_I2S_PIN_NOT_CONNECTED);
    // 16-bit with RATIO_32X is the one combination nrf_i2s_configure allows that
    // lands on exactly 8 kHz; 24-bit rejects 32X outright.
    if (!nrf_i2s_configure(NRF_I2S, NRF_I2S_MODE_MASTER, NRF_I2S_FORMAT_I2S, NRF_I2S_ALIGN_LEFT, NRF_I2S_SWIDTH_16BIT,
                           NRF_I2S_CHANNELS_STEREO, NRF_I2S_MCK_32MDIV125, NRF_I2S_RATIO_32X)) {
        LOG_ERROR("Audio: I2S configure rejected 8 kHz");
        PDM.end();
        captureOn = false;
        return false;
    }
    playbackOn = true;
    txRunning = false;
    txIdx = 0;
    return true;
}

void audioIOEnd()
{
    if (captureOn) {
        PDM.end();
        captureOn = false;
    }
    if (playbackOn) {
        nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
        nrf_i2s_disable(NRF_I2S);
        playbackOn = false;
        txRunning = false;
    }
}

size_t audioIORead(int16_t *dst, size_t samples, uint32_t timeoutMs)
{
    if (!captureOn)
        return 0;

    const uint32_t start = millis();
    size_t out = 0;
    int16_t in[64];

    while (out < samples && Throttle::isWithinTimespanMs(start, timeoutMs)) {
        const int availBytes = PDM.available();
        if (availBytes <= 0) {
            yield();
            continue;
        }
        size_t want = (size_t)availBytes;
        if (want > sizeof(in))
            want = sizeof(in);
        const int gotBytes = PDM.read(in, want);
        if (gotBytes <= 0)
            continue;

        for (int i = 0; i < gotBytes / (int)sizeof(int16_t) && out < samples; i++) {
            const int16_t filtered = firStep(in[i]);
            firPhase = !firPhase;
            if (!firPhase) // keep every second sample: 16 kHz -> 8 kHz
                dst[out++] = filtered;
        }
    }
    return out;
}

size_t audioIOWrite(const int16_t *src, size_t samples, uint32_t timeoutMs)
{
    if (!playbackOn)
        return 0;
    if (samples > AUDIO_FRAME_SAMPLES_MAX)
        samples = AUDIO_FRAME_SAMPLES_MAX;

    uint32_t *buf = txBuf[txIdx];
    for (size_t i = 0; i < samples; i++)
        buf[i] = ((uint32_t)(uint16_t)src[i] << 16) | (uint16_t)src[i];

    if (!txRunning) {
        nrf_i2s_transfer_set(NRF_I2S, (uint16_t)samples, NULL, buf);
        nrf_i2s_enable(NRF_I2S);
        nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);
        txRunning = true;
    } else {
        // EasyDMA wants the next pointer at TXPTRUPD; miss it and the previous
        // buffer replays.
        const uint32_t start = millis();
        while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
            if (!Throttle::isWithinTimespanMs(start, timeoutMs))
                return 0;
            yield();
        }
        nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);
        nrf_i2s_transfer_set(NRF_I2S, (uint16_t)samples, NULL, buf);
    }

    txIdx ^= 1;
    return samples;
}

#endif
