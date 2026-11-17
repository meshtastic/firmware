#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO && defined(ARCH_NRF52)

#include "AudioIO.h"
#include "mesh/Throttle.h"
#include <nrf_i2s.h>
#include <nrf_pdm.h>

#if !defined(MIC_DATA) || !defined(MIC_SCLK)
#error "HAS_AUDIO_CODEC2 on nRF52 needs MIC_DATA and MIC_SCLK in variant.h"
#endif
#if !defined(SPEAKER_BCLK) || !defined(SPEAKER_WS_LRCK) || !defined(SPEAKER_DATA)
#error "HAS_AUDIO_CODEC2 on nRF52 needs SPEAKER_BCLK, SPEAKER_WS_LRCK and SPEAKER_DATA in variant.h"
#endif

// The PDM peripheral cannot clock below 16 kHz: its slowest ratio is
// 1.280 MHz / RATIO80, so capture runs at 16 kHz and decimates 2:1. Playback is
// exact: 32 MHz / 125 / 32 = 8000 Hz.
#define PDM_CAPTURE_HZ 16000

// 11-tap half-band low-pass, cutoff Fs/4, Q15. Without it the 4-8 kHz band
// folds straight back onto the speech when decimating. -1.9 dB at 3 kHz,
// -28.7 dB at 6 kHz, -43.8 dB at 8 kHz. Alternate taps are zero by
// construction, so it costs six multiplies per output sample.
static constexpr int FIR_TAPS = 11;
static constexpr int16_t FIR[FIR_TAPS] = {166, 0, -1374, 0, 9453, 16278, 9453, 0, -1374, 0, 166};

static int16_t firHist[FIR_TAPS];
static bool firPhase;

uint32_t audioPdmOverruns; // diagnostic, see AudioIO.h
static bool captureOn;
static bool playbackOn;
static bool txRunning; // EasyDMA started - the HAL has no enable-check

// Stereo frames, same sample in both halves so the amp's channel strap does not
// matter. Two buffers so EasyDMA always has somewhere to go.
static uint32_t txBuf[2][AUDIO_FRAME_SAMPLES_MAX];
static uint8_t txIdx;

// The core's PDM library is not usable here: PDMDoubleBuffer only allocates
// inside setBufferSize(), and begin() triggers TASK_START with SAMPLE.PTR still
// unset. Both end as an EasyDMA write to address 0 - internal flash - which
// faults the board, and priming around them was not enough. The peripheral is
// small enough to drive directly, which is what the playback half already does.
//
// Polled ring, no interrupt: EasyDMA fills one slot while audioIORead drains
// another, and the events are checked in line. Nothing runs behind the
// cooperative scheduler's back.
//
// Four slots rather than two. With two, a read that stops mid slot because the
// codec2 frame is filled loses the remainder when the next END arrives, which
// is audible as a click on a 64 sample boundary. The ring holds a partly
// drained slot until it is finished and only ever discards whole slots.
// GAIN runs 0x00 (-20 dB) to 0x50 (+20 dB), 0x28 being 0 dB. The MP34DT05 is a
// low-sensitivity part and the vendor firmware asks for the maximum, so match
// it: at 0 dB speech sits near the noise floor and decodes as static.
#ifndef AUDIO_PDM_GAIN
#define AUDIO_PDM_GAIN 0x50
#endif

#define PDM_CHUNK 128 // samples per half, 8 ms at 16 kHz
// EasyDMA writes only to word-aligned addresses, and an int16_t array is
// only guaranteed 2-byte alignment.
// Eight slots, 64 ms. Capture and encode are serialised in captureAndEncode:
// audioIORead waits for a frame, then codec2_encode runs ~30 ms with nothing
// draining the microphone. Four slots hold 32 ms, so the encode outruns the
// buffer and the sender cannot fill packets in real time.
#define PDM_RING 8
static __attribute__((aligned(4))) int16_t pdmBuf[PDM_RING][PDM_CHUNK];
static uint8_t pdmArmed;  // slot last handed to EasyDMA
static uint8_t pdmHead;   // slot EasyDMA is writing
static uint8_t pdmTail;   // slot audioIORead is consuming
static uint8_t pdmFull;   // slots holding data
static uint16_t pdmTaken; // samples already consumed from the tail slot

static bool pdmStart()
{
    // PDM needs HFCLK. The SoftDevice owns it once enabled, so ask through it.
    uint8_t sdEnabled = 0;
    sd_softdevice_is_enabled(&sdEnabled);
    if (sdEnabled) {
        sd_clock_hfclk_request();
        uint32_t running = 0;
        for (int i = 0; i < 1000 && !running; i++) {
            sd_clock_hfclk_is_running(&running);
            if (!running)
                delayMicroseconds(10);
        }
        if (!running) {
            LOG_ERROR("Audio: HFCLK would not start for PDM");
            return false;
        }
    } else if (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {
        NRF_CLOCK->TASKS_HFCLKSTART = 1;
        for (int i = 0; i < 1000 && !NRF_CLOCK->EVENTS_HFCLKSTARTED; i++)
            delayMicroseconds(10);
    }

    pinMode(MIC_SCLK, OUTPUT);
    digitalWrite(MIC_SCLK, LOW);
    pinMode(MIC_DATA, INPUT);

    // 1.280 MHz with RATIO80 is 16 kHz, the lowest the peripheral reaches.
    nrf_pdm_clock_set(NRF_PDM, NRF_PDM_FREQ_1280K);
    NRF_PDM->RATIO = (PDM_RATIO_RATIO_Ratio80 << PDM_RATIO_RATIO_Pos) & PDM_RATIO_RATIO_Msk;
    nrf_pdm_mode_set(NRF_PDM, NRF_PDM_MODE_MONO, NRF_PDM_EDGE_LEFTFALLING);
    nrf_pdm_gain_set(NRF_PDM, AUDIO_PDM_GAIN, AUDIO_PDM_GAIN);
    nrf_pdm_psel_connect(NRF_PDM, digitalPinToPinName(MIC_SCLK), digitalPinToPinName(MIC_DATA));

    pdmArmed = 0;
    pdmHead = PDM_RING - 1; // first END advances it to slot 0
    pdmTail = 0;
    pdmFull = 0;
    pdmTaken = 0;

    nrf_pdm_event_clear(NRF_PDM, NRF_PDM_EVENT_STARTED);
    nrf_pdm_event_clear(NRF_PDM, NRF_PDM_EVENT_END);
    nrf_pdm_event_clear(NRF_PDM, NRF_PDM_EVENT_STOPPED);
    nrf_pdm_int_disable(NRF_PDM, 0xFFFFFFFF); // polled; no handler is installed

    // Armed before START, which is exactly what the core library fails to do.
    nrf_pdm_buffer_set(NRF_PDM, (uint32_t *)pdmBuf[pdmArmed], PDM_CHUNK);
    nrf_pdm_enable(NRF_PDM);
    nrf_pdm_task_trigger(NRF_PDM, NRF_PDM_TASK_START);
    return true;
}

static void pdmStop()
{
    nrf_pdm_event_clear(NRF_PDM, NRF_PDM_EVENT_STOPPED);
    nrf_pdm_task_trigger(NRF_PDM, NRF_PDM_TASK_STOP);
    for (int i = 0; i < 1000 && !nrf_pdm_event_check(NRF_PDM, NRF_PDM_EVENT_STOPPED); i++)
        delayMicroseconds(10);
    nrf_pdm_disable(NRF_PDM);

    uint8_t sdEnabled = 0;
    sd_softdevice_is_enabled(&sdEnabled);
    if (sdEnabled)
        sd_clock_hfclk_release();
}

// Hand EasyDMA the next half as soon as it reports STARTED, and mark the one it
// just finished as drainable on END.
static void pdmService()
{
    if (nrf_pdm_event_check(NRF_PDM, NRF_PDM_EVENT_STARTED)) {
        nrf_pdm_event_clear(NRF_PDM, NRF_PDM_EVENT_STARTED);
        // The peripheral has taken the pointer we armed and is filling it, so
        // give it the slot after that one.
        pdmArmed = (uint8_t)((pdmArmed + 1) % PDM_RING);
        nrf_pdm_buffer_set(NRF_PDM, (uint32_t *)pdmBuf[pdmArmed], PDM_CHUNK);
    }
    if (nrf_pdm_event_check(NRF_PDM, NRF_PDM_EVENT_END)) {
        nrf_pdm_event_clear(NRF_PDM, NRF_PDM_EVENT_END);
        pdmHead = (uint8_t)((pdmHead + 1) % PDM_RING);
        if (pdmFull < PDM_RING) {
            pdmFull++;
        } else {
            // Ring is full, so the oldest slot is being overwritten. Drop it
            // whole: a partial slot would splice two discontinuous points.
            audioPdmOverruns++;
            pdmTail = (uint8_t)((pdmTail + 1) % PDM_RING);
            pdmTaken = 0;
        }
    }
}

static void enablePin(int pin, bool on)
{
    if (pin < 0)
        return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, on ? HIGH : LOW);
}

// The PDM mic has no enable line; the amplifier does, and on the t-echo-card it
// takes two. Without these the I2S clocks run into a muted amp and nothing is
// heard.
void audioIOEnable(bool capture, bool playback)
{
    if (capture && !captureOn) {
        memset(firHist, 0, sizeof(firHist));
        firPhase = false;
        captureOn = pdmStart();
        if (!captureOn)
            LOG_ERROR("Audio: PDM would not start");
    } else if (!capture && captureOn) {
        pdmStop();
        captureOn = false;
    }
    // Muting the amplifier is not enough: EasyDMA keeps cycling whatever it
    // played last, so a finished stream would loop its finalseconds silently
    // into a re-enabled amp. Stop the transfer with the amp.
    if (!playback && txRunning) {
        nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
        for (int i = 0; i < 100 && !nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_STOPPED); i++)
            delayMicroseconds(50);
        nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_STOPPED);
        nrf_i2s_disable(NRF_I2S);
        txRunning = false;
    }

#ifdef SPEAKER_EN
    enablePin(SPEAKER_EN, playback);
#endif
#ifdef SPEAKER_EN_2
    enablePin(SPEAKER_EN_2, playback);
#endif
#if !defined(SPEAKER_EN) && !defined(SPEAKER_EN_2)
    (void)playback;
#endif
}

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

// System tones share NRF_I2S through NRF52I2SOutput, which configures it for its
// own clock and calls nrf_i2s_disable() when the tone ends. So playback settings
// are re-applied per burst rather than once at startup.
static bool configurePlayback()
{
    nrf_i2s_pins_set(NRF_I2S, SPEAKER_BCLK, SPEAKER_WS_LRCK, NRF_I2S_PIN_NOT_CONNECTED, SPEAKER_DATA, NRF_I2S_PIN_NOT_CONNECTED);
    // 16-bit with RATIO_32X is the one combination nrf_i2s_configure allows that
    // lands on exactly 8 kHz; 24-bit rejects 32X outright.
    return nrf_i2s_configure(NRF_I2S, NRF_I2S_MODE_MASTER, NRF_I2S_FORMAT_I2S, NRF_I2S_ALIGN_LEFT, NRF_I2S_SWIDTH_16BIT,
                             NRF_I2S_CHANNELS_STEREO, NRF_I2S_MCK_32MDIV125, NRF_I2S_RATIO_32X);
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

    // Survives a reset: how far pdmStart/audioIORead got before the last fault.
    memset(firHist, 0, sizeof(firHist));
    firPhase = false;

    // PDM is not started here: half duplex, so the microphone only runs while
    // keyed up. audioIOEnable() brings it up on the PTT edge.

    if (!configurePlayback()) {
        LOG_ERROR("Audio: I2S configure rejected 8 kHz");
        return false;
    }
    playbackOn = true;
    txRunning = false;
    txIdx = 0;
    audioIOEnable(false, false); // the module powers the amp per stream
    return true;
}

void audioIOEnd()
{
    audioIOEnable(false, false);
    if (captureOn) {
        pdmStop();
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

    while (out < samples && Throttle::isWithinTimespanMs(start, timeoutMs)) {
        pdmService();
        if (!pdmFull) {
            yield();
            continue;
        }
        while (pdmTaken < PDM_CHUNK && out < samples) {
            const int16_t filtered = firStep(pdmBuf[pdmTail][pdmTaken++]);
            firPhase = !firPhase;
            if (!firPhase) // keep every second sample: 16 kHz -> 8 kHz
                dst[out++] = filtered;
        }
        // Release the slot only once it is empty. Stopping mid slot because the
        // frame is full is normal, and the remainder is picked up next call.
        if (pdmTaken >= PDM_CHUNK) {
            pdmTaken = 0;
            pdmTail = (uint8_t)((pdmTail + 1) % PDM_RING);
            pdmFull--;
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

    // A tone since our last burst leaves ENABLE clear and the amp muted, so trust
    // the peripheral rather than txRunning alone.
    if (!txRunning || !NRF_I2S->ENABLE) {
        if (!configurePlayback())
            return 0;
        audioIOEnable(false, true); // the tone path pulls the amp low when it ends
        uint32_t *first = txBuf[txIdx];
        for (size_t i = 0; i < samples; i++)
            first[i] = ((uint32_t)(uint16_t)src[i] << 16) | (uint16_t)src[i];
        nrf_i2s_transfer_set(NRF_I2S, (uint16_t)samples, NULL, first);
        nrf_i2s_enable(NRF_I2S);
        nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);
        txRunning = true;
        txIdx ^= 1;
        return samples;
    }

    // Wait before filling, not after. TXPTRUPD says the peripheral has latched
    // the pointer we gave it last time, which is the moment the other buffer
    // stops being read. Filling first scribbles over audio EasyDMA is still
    // playing, and a missed update replays the previous buffer: a 40 ms block
    // of noise, repeating.
    const uint32_t start = millis();
    while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
        if (!Throttle::isWithinTimespanMs(start, timeoutMs))
            return 0;
        yield();
    }
    nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);

    uint32_t *buf = txBuf[txIdx];
    for (size_t i = 0; i < samples; i++)
        buf[i] = ((uint32_t)(uint16_t)src[i] << 16) | (uint16_t)src[i];
    nrf_i2s_transfer_set(NRF_I2S, (uint16_t)samples, NULL, buf);

    txIdx ^= 1;
    return samples;
}

#endif
