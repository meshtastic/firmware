#pragma once

#include "Observer.h"
#include "audio/RtttlPcm.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <atomic>
#include <memory>

#ifdef HAS_I2S

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class MeshtasticI2SOut;

/**
 * I2S playback for tones and ringtones.
 *
 * The public API is unchanged from the ESP8266Audio implementation this replaces, minus
 * readAloud(): BackgroundAudio has no SAM equivalent, its espeak-ng speech costs ~947KB of
 * flash and ~88KB of permanent internal DRAM, and ESP8266SAM cannot be kept alongside it -
 * espeak's `SetSpeed`/`speed` globals collide with SAM's at link time. Text-to-speech is
 * therefore dropped for now.
 *
 * The other difference that matters: playback is asynchronous. Samples are generated on
 * demand by RtttlPcm and pushed into the I2S DMA ring by a small per-playback feeder
 * task. The feeder exists because the main loop cannot be trusted to run often enough:
 * on MUI builds the tft task holds spiLock across entire LVGL cycles (render plus the
 * SPI flush), and on shared-SPI-bus boards like T-Deck the main loop then blocks behind
 * it on every radio operation - longer than any affordable DMA ring, which was audible
 * as stuttering. The feeder touches only the generator and the I2S channel, never
 * spiLock, so display and radio contention cannot starve it. Note this is NOT upstream
 * BackgroundAudio's task coming back: that one exists to maintain availableForWrite()
 * from ISR notifications, which is the machinery MeshtasticI2SOut deliberately avoids -
 * the feeder uses the same ask-the-IDF-directly zero-timeout writes as before. If the
 * task cannot be created, feeding falls back to the main loop (runOnce/isPlaying), the
 * pre-feeder behavior.
 *
 * isPlaying() stays true until the DMA has actually drained, not just until the last
 * sample was queued - the amplifier must not be cut before the audio has physically
 * left the pin.
 *
 * The I2S channel is allocated per playback and released when idle, matching what
 * ESP8266Audio did, so an idle board draws no more than it used to; the feeder task
 * and its stack likewise only exist while a melody is in flight.
 */
class AudioThread : public concurrency::OSThread
{
  public:
    AudioThread();
    ~AudioThread();

    /// Play an RTTTL string (a user ringtone). Malformed songs are ignored.
    void beginRttl(const void *data, uint32_t len);

    /// Play a system melody directly, without going via RTTTL.
    void beginTones(const ToneDuration *tones, size_t count);

    /// True while anything is still playing or draining. Also services the DMA, so it is
    /// safe - and useful - for callers to poll it.
    bool isPlaying();

    /// Stop immediately and power the amplifier down. Safe to call when nothing is
    /// playing; ExternalNotificationModule::stopNow() relies on that (see 2ae391197).
    void stop();

    /// Veto light sleep while audio is in flight - sleeping gates the I2S peripheral
    /// clock, which would truncate playback.
    int preflightSleepCb(void *unused) { return isIdle() ? 0 : 1; }

    CallbackObserver<AudioThread, void *> preflightSleepObserver =
        CallbackObserver<AudioThread, void *>(this, &AudioThread::preflightSleepCb);

  protected:
    int32_t runOnce() override;

  private:
    enum class State { IDLE, PLAYING, DRAINING };

    /// Frames generated and handed to the DMA per write. Need not match the DMA buffer
    /// size; writes land mid-descriptor just fine.
    static constexpr size_t kStagingFrames = 128;

    /// Silence preloaded ahead of the first real sample, giving the just-enabled amp and
    /// the codec unmute ramp time to settle. Also the entire startup latency of a click.
    static constexpr size_t kAmpLeadInFrames = (RtttlPcm::kSampleRate * 10) / 1000; // 10ms

    /// Extra quiet time after the DMA has drained, before the amp is cut.
    static constexpr uint32_t kSettleMs = 20;

    /// If the DMA accepts nothing for this long while playing, something is wrong with the
    /// channel. Give up rather than leave the amplifier powered indefinitely. A real song
    /// makes progress every tick, since a DMA buffer frees roughly every 12ms.
    static constexpr uint32_t kStallTimeoutMs = 1000;

    static constexpr int32_t kActiveIntervalMs = 10;
    static constexpr int32_t kIdleIntervalMs = 100;

    /// Feeder task sizing. The loop is RtttlPcm math plus a zero-timeout I2S write, with
    /// LOG_WARN reachable on the telemetry path; 4KB covers that comfortably. Priority 2
    /// (above loopTask's 1) and pinned to the same core as loopTask, so it preempts only
    /// the loop it replaces, briefly, and stays off core 0 where the radio ISRs, BLE and
    /// the MUI tft task live. It sleeps kFeederIntervalMs between fills; a 256-frame DMA
    /// buffer drains in ~11.6ms, so 5ms polling can never miss the ring by more than half
    /// a buffer.
    static constexpr uint32_t kFeederStackBytes = 4096;
    static constexpr UBaseType_t kFeederPriority = 2;
    static constexpr BaseType_t kFeederCore = 1;
    static constexpr uint32_t kFeederIntervalMs = 5;

    bool isIdle() const { return state == State::IDLE; }

    /// Arm the sink and start feeding, assuming the generator is already loaded.
    bool startPlayback();
    /// Release the channel and power down the amp. Idempotent.
    void stopPlayback();
    /// Fill the disabled DMA ring (lead-in silence, then melody) before start().
    void preloadRing();
    /// Move samples into the DMA until it is full or the song ends. Main-loop fallback,
    /// used only when the feeder task could not be created.
    void pump();
    /// Body of the feeder task: pump until the melody ends, a stall is detected, or
    /// joinFeeder() asks it to quit. Owns generator/staging while it runs.
    void feederLoop();
    static void feederEntry(void *self) { static_cast<AudioThread *>(self)->feederLoop(); }
    /// React (on the main loop) to the feeder finishing or stalling.
    void serviceFeeder();
    /// Signal the feeder and wait for it to exit. Must precede sink->end() or any
    /// generator re-arm - the channel and generator must not be torn down under a live
    /// writer. Bounded: the feeder checks the flag at least every kFeederIntervalMs.
    /// Returns false only if the feeder failed to exit (teardown must then be skipped).
    bool joinFeeder();
    /// Hold on until the queued audio has physically played out.
    void beginDrain();
    void ampEnable(bool on);

    std::unique_ptr<MeshtasticI2SOut> sink;

    RtttlPcm generator;

    // Frames generated but not yet accepted by the DMA. generate() is destructive, so a
    // short write has to be carried across pump() calls rather than regenerated.
    int16_t staging[kStagingFrames * 2] = {};
    size_t stagedFrames = 0;
    size_t stagedOffset = 0;

    State state = State::IDLE;
    uint32_t drainUntil = 0;
    uint32_t lastProgressMs = 0;

    /// When pump() last ran while PLAYING. A gap longer than the ring's drain time is a
    /// proven underrun - logged so testers can attribute dropouts to main-loop stalls.
    uint32_t lastPumpMs = 0;

    /// Feeder task lifecycle. The handle is only ever touched from the main loop; the
    /// flags are the sole cross-task communication. feederExited doubles as the join
    /// signal - once true the task has passed its last generator/sink access and is
    /// about to self-delete, so teardown may proceed.
    TaskHandle_t feederTask = nullptr;
    std::atomic<bool> feederStop{false};
    std::atomic<bool> feederExited{false};
    std::atomic<bool> feederStalled{false};

    /// Tracks the amp enable so it is only driven on an actual change. On the two boards
    /// that have one it is an I2C expander write, and needlessly cycling it off and on at
    /// the start of every tone is audible as a pop (see commit 42e475963).
    bool ampOn = false;
};

#endif // HAS_I2S
