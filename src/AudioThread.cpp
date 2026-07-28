#include "AudioThread.h"

#ifdef HAS_I2S

#include "platform/esp32/MeshtasticI2SOut.h"
#include "sleep.h"

// A board with an I2S amplifier opts in by defining AUDIO_AMP_ENABLE(on) in its
// variant.h to power the amp on/off around playback (e.g. an enable pin on an I/O
// expander). The includes below expose the expander instances (io / mcpIoExpander) those
// macros typically reference. Only tlora-pager and meshnology-w10 define it; on the other
// four HAS_I2S boards the speaker is always connected, which is why MeshtasticI2SOut sets
// auto_clear so the DMA emits silence rather than replaying its last buffer.
#ifdef USE_XL9555
#include "ExtensionIOXL9555.hpp"
extern ExtensionIOXL9555 io;
#endif

#ifdef USE_MCP23017
#include "platform/esp32/ExtensionIOMCP23017.h"
#endif

AudioThread::AudioThread() : OSThread("Audio")
{
    // Deliberately no hardware access here. This runs from setup() at main.cpp:1034,
    // before lateInitVariant() at :1134, so on meshnology-w10 the amp enable pin is not
    // an output yet and driving it would be a silent no-op. Boot-time amp-off already
    // comes from variant code (see main.cpp:377 for tlora-pager) - commit 42e475963.
    sink = std::make_unique<MeshtasticI2SOut>(DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_MCLK);
    // Only records the rate; the hardware is not touched until begin().
    sink->setFrequency(RtttlPcm::kSampleRate);

    preflightSleepObserver.observe(&preflightSleep);
}

AudioThread::~AudioThread()
{
    stopPlayback();
}

void AudioThread::ampEnable(bool on)
{
    // Only drive it on a real change: back-to-back tones would otherwise cut the amp and
    // immediately re-enable it, which is audible as a pop.
    if (on == ampOn)
        return;
    ampOn = on;

#ifdef AUDIO_AMP_ENABLE
    // Must stay on this thread: on both boards that have an amp enable this is a blocking
    // I2C expander write, and issuing it from another task would race the main loop's
    // other I2C users.
    AUDIO_AMP_ENABLE(on);
#endif
}

bool AudioThread::startPlayback()
{
    stopPlayback(); // release anything already running

    // Amp first, so it has settled before the first non-silent sample arrives.
    ampEnable(true);

    if (!sink->begin()) {
        LOG_ERROR("Audio: could not start I2S");
        ampEnable(false);
        generator.reset();
        return false;
    }

    stagedFrames = 0;
    stagedOffset = 0;
    lastProgressMs = millis();
    state = State::PLAYING;

    pump(); // prime the DMA now so playback starts without waiting for a tick
    setIntervalFromNow(0);
    return true;
}

void AudioThread::stopPlayback()
{
    stagedFrames = 0;
    stagedOffset = 0;
    if (sink)
        sink->end();
    ampEnable(false);
    state = State::IDLE;
    canSleep = true;
}

void AudioThread::beginDrain()
{
    // done() only means the generator has no more samples; up to a full DMA ring is still
    // queued for the hardware. Cutting the amp now clips the tail and pops.
    drainUntil = millis() + (sink ? sink->dmaDrainMs() : 50) + kSettleMs;
    state = State::DRAINING;
}

void AudioThread::pump()
{
    if (state != State::PLAYING || !sink)
        return;

    for (;;) {
        // Finish handing over anything the DMA refused last time first.
        if (stagedOffset < stagedFrames) {
            size_t wrote = sink->writeFrames(staging + (stagedOffset * 2), stagedFrames - stagedOffset);
            if (!wrote)
                return; // DMA full; come back next tick
            lastProgressMs = millis();
            stagedOffset += wrote;
            if (stagedOffset < stagedFrames)
                return;
        }

        stagedOffset = 0;
        stagedFrames = generator.generate(staging, kStagingFrames);
        if (!stagedFrames) {
            beginDrain();
            return;
        }
    }
}

void AudioThread::beginRttl(const void *data, uint32_t len)
{
    if (!sink || !data)
        return;

    if (!generator.begin((const char *)data, len)) {
        LOG_WARN("Audio: ignoring malformed RTTTL");
        return;
    }
    startPlayback();
}

void AudioThread::beginTones(const ToneDuration *tones, size_t count)
{
    if (!sink || !tones || !count)
        return;

    if (!generator.beginTones(tones, count))
        return;
    startPlayback();
}

bool AudioThread::isPlaying()
{
    // Doubles as the pump so that callers polling this keep audio flowing even if this
    // thread is starved - which is how ExternalNotificationModule has always driven it.
    if (state == State::PLAYING)
        pump();
    else if (state == State::DRAINING && millis() >= drainUntil)
        stopPlayback();

    return !isIdle();
}

void AudioThread::stop()
{
    generator.reset();
    stopPlayback();
}

int32_t AudioThread::runOnce()
{
    switch (state) {
    case State::PLAYING:
        pump();
        canSleep = false;
        // Never let a wedged I2S channel leave the amplifier powered forever.
        if (state == State::PLAYING && (millis() - lastProgressMs) > kStallTimeoutMs) {
            LOG_ERROR("Audio: I2S stalled, abandoning playback");
            stop();
            return kIdleIntervalMs;
        }
        return kActiveIntervalMs;

    case State::DRAINING:
        canSleep = false;
        if (millis() >= drainUntil) {
            stopPlayback();
            return kIdleIntervalMs;
        }
        return kActiveIntervalMs;

    case State::IDLE:
    default:
        canSleep = true;
        return kIdleIntervalMs;
    }
}

#endif // HAS_I2S
