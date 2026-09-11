#pragma once

#include "audio/NotificationAudio.h"
#include "A7682AudioPolicy.h"
#include "concurrency/OSThread.h"
#include "mesh/TypedQueue.h"

#include <atomic>
#include <cstdint>

class HardwareSerial;

class A7682Audio final : public NotificationAudio, private concurrency::OSThread
{
  public:
    A7682Audio();

    uint8_t getVolume() const override;
    void setVolume(uint8_t volume) override;
    bool queueCue(NotificationAudioCue cue) override;
    void shutdown() override;

  protected:
    int32_t runOnce() override;

  private:
    enum class State : uint8_t {
        STARTUP_WAIT,
        RESET_LOW,
        RESET_HIGH,
        POWER_KEY_LOW,
        POWER_KEY_HIGH,
        WAIT_BOOT,
        READY,
        WAIT_PLAY_START,
        PLAYING,
        BACKOFF,
        OFF,
    };

    enum class Command : uint8_t {
        NONE,
        PROBE,
#if defined(A7682_AUDIO_DEBUG_FILESYSTEM)
        FILESYSTEM_CD,
        FILESYSTEM_LS,
#endif
        GAIN,
        PLAY,
    };

    static constexpr int CUE_QUEUE_CAPACITY = 4;
    static constexpr uint32_t STARTUP_WAIT_MS = 250;
    static constexpr uint32_t RESET_LOW_MS = 2500;
    static constexpr uint32_t RESET_HIGH_MS = 100;
    static constexpr uint32_t POWER_KEY_LOW_MS = 10;
    static constexpr uint32_t POWER_KEY_HIGH_MS = 50;
    static constexpr uint32_t MODEM_BOOT_WAIT_MS = 2000;
    static constexpr uint32_t COMMAND_TIMEOUT_MS = 1500;
    static constexpr uint32_t PLAY_START_TIMEOUT_MS = 2000;
    static constexpr uint32_t PLAY_MAXIMUM_MS = 15000;
    static constexpr uint32_t RETRY_INITIAL_MS = 1000;
    static constexpr uint32_t RETRY_MAXIMUM_MS = 15000;

    TypedQueue<NotificationAudioCue> cueQueue;
    HardwareSerial *serial = nullptr;
    std::atomic<uint8_t> volume{A7682_AUDIO_DEFAULT_VOLUME};
    std::atomic<bool> volumeDirty{false};
    std::atomic<bool> acceptingCues{true};

    State state = State::STARTUP_WAIT;
    Command command = Command::NONE;
    NotificationAudioCue activeCue = NotificationAudioCue::RX_TEXT;
    uint8_t commandVolume = A7682_AUDIO_DEFAULT_VOLUME;
    uint32_t stateStartedAt = 0;
    uint32_t commandStartedAt = 0;
    uint32_t commandTimeoutMs = COMMAND_TIMEOUT_MS;
    uint32_t playStartedAt = 0;
    uint32_t retryStartedAt = 0;
    uint32_t retryDelayMs = RETRY_INITIAL_MS;
    uint8_t retryCount = 0;
    bool resetTried = false;
    bool powerKeyTried = false;
    bool audioStarted = false;
    char lineBuffer[128] = {};
    size_t lineLength = 0;

    uint8_t loadSettings();
    bool saveSettings() const;
    void beginModem();
    void startResetSequence();
    void startPowerKeyPulse();
    void pollModem();
    void processLine(const char *line);
    void sendCommand(Command command, const char *text, uint32_t timeoutMs = COMMAND_TIMEOUT_MS);
    void sendGainCommand();
    void startPlayback(NotificationAudioCue cue);
    void commandSucceeded();
    void commandFailed();
    void enterBackoff();
};
