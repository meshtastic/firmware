#include "AudioThread.h"

#ifdef HAS_I2S

#include "platform/esp32/MeshtasticI2SOut.h"
#include "sleep.h"
#include <cstring>

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

    // Fill the still-disabled DMA ring before the channel starts clocking. Playback then
    // begins with the ring at maximum depth, which matters because the event that starts
    // a melody is usually the same one that kicks the screen into back-to-back frame
    // pushes on this loop - the exact stall the ring must ride out.
    preloadRing();

    if (!sink->start()) {
        stopPlayback();
        generator.reset();
        return false;
    }

    lastProgressMs = lastPumpMs = millis();
    state = State::PLAYING;

    // Hand the feeding to a dedicated task, so audio survives arbitrary main-loop
    // stalls - on MUI builds the loop routinely blocks on spiLock behind whole LVGL
    // flushes (see the class comment). The ring is already fully preloaded, so even the
    // task creation itself is not time-critical. On failure, fall back to feeding from
    // runOnce()/isPlaying() as before.
    feederStop.store(false);
    feederExited.store(false);
    feederStalled.store(false);
    if (xTaskCreatePinnedToCore(feederEntry, "AudioFeed", kFeederStackBytes, this, kFeederPriority, &feederTask, kFeederCore) !=
        pdPASS) {
        feederTask = nullptr;
        LOG_WARN("Audio: no RAM for feeder task, feeding from main loop");
    }

    setIntervalFromNow(0);
    return true;
}

void AudioThread::feederLoop()
{
    uint32_t lastProgress = millis();
    uint32_t lastFed = millis();
    bool done = false;

    while (!done && !feederStalled.load() && !feederStop.load()) {
        // Same dropout telemetry as pump(): with priority 2 this should never fire, so
        // if it does something is preempting even the feeder and testers should know.
        uint32_t now = millis();
        if ((now - lastFed) > sink->dmaDrainMs())
            LOG_WARN("Audio: fed after %ums but ring holds %ums - dropout", (unsigned)(now - lastFed),
                     (unsigned)sink->dmaDrainMs());
        lastFed = now;

        for (;;) {
            // Finish handing over anything the DMA refused last time first.
            if (stagedOffset < stagedFrames) {
                size_t wrote = sink->writeFrames(staging + (stagedOffset * 2), stagedFrames - stagedOffset);
                if (!wrote)
                    break; // ring full; sleep and retry
                lastProgress = millis();
                stagedOffset += wrote;
                if (stagedOffset < stagedFrames)
                    break; // partial accept: ring full
                continue;
            }

            stagedOffset = 0;
            stagedFrames = generator.generate(staging, kStagingFrames);
            if (!stagedFrames) {
                done = true; // melody complete; runOnce()/isPlaying() will start the drain
                break;
            }
        }

        if (!done) {
            // Never let a wedged channel keep the task (and the amp) alive forever.
            if ((millis() - lastProgress) > kStallTimeoutMs)
                feederStalled.store(true);
            else
                vTaskDelay(pdMS_TO_TICKS(kFeederIntervalMs));
        }
    }

    // Last generator/sink access is above this line - after the store, the main loop is
    // free to tear the channel down.
    feederExited.store(true);
    vTaskDelete(nullptr);
}

void AudioThread::serviceFeeder()
{
    if (feederStalled.load()) {
        LOG_ERROR("Audio: I2S stalled, abandoning playback");
        stop();
    } else if (feederExited.load()) {
        beginDrain();
    }
}

bool AudioThread::joinFeeder()
{
    if (!feederTask)
        return true;

    feederStop.store(true);
    // The feeder observes the flag within kFeederIntervalMs; the margin is for scheduler
    // pathology. It self-deletes after setting feederExited, so the handle is never
    // valid to delete from here once the flag is up.
    for (int i = 0; i < 50 && !feederExited.load(); i++)
        vTaskDelay(pdMS_TO_TICKS(1));
    feederTask = nullptr;

    if (!feederExited.load()) {
        // Should be unreachable (the feeder runs above us and sleeps at most 5ms). Leak
        // the task and the channel rather than tearing either down under a live writer.
        LOG_ERROR("Audio: feeder did not exit, leaking task and channel");
        return false;
    }
    return true;
}

void AudioThread::preloadRing()
{
    stagedFrames = 0;
    stagedOffset = 0;

    // A short silent lead-in: the amp was enabled microseconds ago and the codecs run an
    // unmute ramp, so starting the waveform on the very first sample can clip its attack
    // or pop. The silence-preloaded ring used to provide 46ms of cover implicitly; give
    // a deliberate 10ms instead - that is now the entire startup latency of a click.
    memset(staging, 0, sizeof(staging));
    for (size_t remaining = kAmpLeadInFrames; remaining;) {
        size_t chunk = remaining < kStagingFrames ? remaining : kStagingFrames;
        size_t took = sink->preloadFrames(staging, chunk);
        if (!took)
            return; // ring full already (only possible if the lead-in exceeds the ring)
        remaining -= took;
    }

    // Then as much of the melody as fits. A short click fits entirely; a long ringtone
    // fills the ring and carries the remainder into the normal pump() path.
    for (;;) {
        if (stagedOffset < stagedFrames) {
            size_t took = sink->preloadFrames(staging + (stagedOffset * 2), stagedFrames - stagedOffset);
            if (!took)
                return; // ring full; pump() takes over from the carried remainder
            stagedOffset += took;
            continue;
        }

        stagedOffset = 0;
        stagedFrames = generator.generate(staging, kStagingFrames);
        if (!stagedFrames)
            return; // whole melody preloaded; the first pump() will begin the drain
    }
}

void AudioThread::stopPlayback()
{
    bool feederGone = joinFeeder(); // the writer must be gone before the channel is
    stagedFrames = 0;
    stagedOffset = 0;
    if (sink && feederGone)
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

    // Feed-starvation telemetry: if the loop kept us away longer than the ring holds,
    // the DMA ran dry and auto_clear played silence - an audible dropout. Knowing the
    // stall length points at the hog (a TFT frame push, a screen wake, a flash write).
    uint32_t now = millis();
    if (lastPumpMs && (now - lastPumpMs) > sink->dmaDrainMs())
        LOG_WARN("Audio: fed after %ums but ring holds %ums - dropout", (unsigned)(now - lastPumpMs),
                 (unsigned)sink->dmaDrainMs());
    lastPumpMs = now;

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

    // Join any running feeder BEFORE re-arming the generator - it is the feeder's to
    // read while playback is live.
    stopPlayback();

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

    stopPlayback(); // as in beginRttl: no generator re-arm under a live feeder

    if (!generator.beginTones(tones, count))
        return;
    startPlayback();
}

bool AudioThread::isPlaying()
{
    // Historically doubled as the pump so that callers polling this kept audio flowing
    // even when this thread was starved (how ExternalNotificationModule drives it). With
    // the feeder task that is only needed in fallback mode, but the state transitions
    // still benefit from being serviced on every poll.
    if (state == State::PLAYING) {
        if (feederTask)
            serviceFeeder();
        else
            pump();
    } else if (state == State::DRAINING && millis() >= drainUntil)
        stopPlayback();

    return !isIdle();
}

void AudioThread::stop()
{
    stopPlayback(); // joins the feeder; reset the generator only after that
    generator.reset();
}

int32_t AudioThread::runOnce()
{
    switch (state) {
    case State::PLAYING:
        canSleep = false;
        if (feederTask) {
            serviceFeeder();
            return kActiveIntervalMs;
        }
        // Fallback mode: this loop is the feeder.
        pump();
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
