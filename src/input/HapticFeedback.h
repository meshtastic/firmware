#pragma once

#include "configuration.h"

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)

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
#if defined(T_DECK_MAX)
    uint32_t motorPowerChangedAt = 0;
    bool motorPowerOn = false;

    void setMotorPower(bool on);
    void holdMotorPowerAfterStop();
#endif
#endif

    bool loadSettings();
    bool saveSettings() const;
    std::atomic<bool> enabled{true};
};

extern HapticFeedback *hapticFeedback;
void initHapticFeedback();

#endif // HAPTIC_FEEDBACK_PIN || HAS_DRV2605

#else

#ifdef HAPTIC_FEEDBACK_PIN

#include "concurrency/OSThread.h"
#include <stdint.h>

// Non-blocking pulses on a GPIO vibration motor. HAPTIC_FEEDBACK_ACTIVE_LOW inverts polarity.
class HapticFeedback : public concurrency::OSThread
{
  public:
    HapticFeedback();
    void pulse(uint16_t durationMs = 30);
    void armDelayedPulse(uint16_t delayMs, uint16_t durationMs = 30);
    void cancelDelayedPulse();

  protected:
    int32_t runOnce() override;

  private:
    uint32_t pulseOffAt = 0;
    uint32_t delayedPulseAt = 0;
    uint16_t delayedPulseDuration = 0;

    void motorWrite(bool on);
    // Reschedule to the soonest pending event so later arms don't clobber earlier wakes.
    void scheduleNext();
};

extern HapticFeedback *hapticFeedback;
void initHapticFeedback();

#endif // HAPTIC_FEEDBACK_PIN

#endif // target board or HAPTIC_FEEDBACK_PIN
