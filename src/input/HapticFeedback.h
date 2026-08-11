#pragma once

#include "configuration.h"
#include <atomic>
#include <cstddef>
#include <stdint.h>

enum class HapticEffect : uint8_t {
    NONE = 0,
    NAVIGATION = 8,
    SELECT = 1,
    BACK = 5,
    LONG_PRESS = 27,
    PING = 10,
    KEYPRESS = 24,
    MESSAGE = 16,
};

struct HapticPreferenceRecord {
    uint32_t magic;
    uint8_t version;
    uint8_t enabled;
    uint16_t reserved;
};

static constexpr uint32_t HAPTIC_PREFERENCE_MAGIC = 0x48505443;
static constexpr uint8_t HAPTIC_PREFERENCE_VERSION = 1;

HapticEffect hapticEffectForInputEvent(uint8_t inputEvent);
bool isValidHapticPreferenceRecord(const HapticPreferenceRecord &record, size_t bytesRead);
bool shouldPlayHapticEffect(bool enabled, HapticEffect effect);

#if defined(HAPTIC_FEEDBACK_PIN) || defined(HAS_DRV2605)

#include "concurrency/OSThread.h"
#ifdef HAS_DRV2605
#include "concurrency/Lock.h"
#endif

class HapticFeedback : public concurrency::OSThread
{
  public:
    HapticFeedback();

    bool isEnabled() const;
    void setEnabled(bool enabled);
    void play(HapticEffect effect);
    void stop();

#ifdef HAPTIC_FEEDBACK_PIN
    void pulse(uint16_t durationMs = 30);
    void armDelayedPulse(uint16_t delayMs, uint16_t durationMs = 30);
    void cancelDelayedPulse();
#endif

  protected:
    int32_t runOnce() override;

  private:
#ifdef HAPTIC_FEEDBACK_PIN
    uint32_t pulseStartedAt = 0;
    uint32_t delayedPulseStartedAt = 0;
    uint16_t pulseDuration = 0;
    uint16_t delayedPulseDelay = 0;
    uint16_t delayedPulseDuration = 0;
    bool pulseActive = false;
    bool delayedPulsePending = false;

    void motorWrite(bool on);
    void scheduleNext();
#endif

#ifdef HAS_DRV2605
    concurrency::Lock drvLock;
    HapticEffect configuredEffect = HapticEffect::NONE;
#endif

    bool loadSettings();
    bool saveSettings() const;
    std::atomic<bool> enabled{true};
};

extern HapticFeedback *hapticFeedback;
void initHapticFeedback();

#endif // HAPTIC_FEEDBACK_PIN || HAS_DRV2605
