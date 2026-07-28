#pragma once

#include "Observer.h"
#include "audio/RtttlPcm.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <memory>

#ifdef HAS_I2S

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
 * The other difference that matters: playback is asynchronous. Samples are
 * generated on demand by RtttlPcm and pushed into the I2S DMA ring a chunk at a time,
 * either from runOnce() or from isPlaying(). isPlaying() stays true until the DMA has
 * actually drained, not just until the last sample was queued - the amplifier must not
 * be cut before the audio has physically left the pin.
 *
 * The I2S channel is allocated per playback and released when idle, matching what
 * ESP8266Audio did, so an idle board draws no more than it used to.
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

    /// Frames handed to the DMA per write. One DMA buffer's worth.
    static constexpr size_t kStagingFrames = 128;

    /// Extra quiet time after the DMA has drained, before the amp is cut.
    static constexpr uint32_t kSettleMs = 20;

    /// If the DMA accepts nothing for this long while playing, something is wrong with the
    /// channel. Give up rather than leave the amplifier powered indefinitely. A real song
    /// makes progress every tick, since a DMA buffer frees roughly every 46ms.
    static constexpr uint32_t kStallTimeoutMs = 1000;

    static constexpr int32_t kActiveIntervalMs = 10;
    static constexpr int32_t kIdleIntervalMs = 100;

    bool isIdle() const { return state == State::IDLE; }

    /// Arm the sink and start feeding, assuming the generator is already loaded.
    bool startPlayback();
    /// Release the channel and power down the amp. Idempotent.
    void stopPlayback();
    /// Move samples into the DMA until it is full or the song ends.
    void pump();
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

    /// Tracks the amp enable so it is only driven on an actual change. On the two boards
    /// that have one it is an I2C expander write, and needlessly cycling it off and on at
    /// the start of every tone is audible as a pop (see commit 42e475963).
    bool ampOn = false;
};

#endif // HAS_I2S
