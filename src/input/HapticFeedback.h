#pragma once

#include "input/HapticOutput.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

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

#include "concurrency/OSThread.h"

class HapticFeedback : public concurrency::OSThread
{
  public:
    HapticFeedback();

    bool isEnabled() const;
    void setEnabled(bool enabled);
    void play(HapticEffect effect);
    void stop();
    void pulse(uint16_t durationMs = 30);
    void armDelayedPulse(uint16_t delayMs, uint16_t durationMs = 30);
    void cancelDelayedPulse();

  protected:
    int32_t runOnce() override;

  private:
    HapticOutput *output = nullptr;
    bool outputReady = false;

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

    bool loadSettings();
    bool saveSettings() const;
    std::atomic<bool> enabled{true};
};

extern HapticFeedback *hapticFeedback;
void initHapticFeedback();

// Shared touch/input code calls this capability-neutral entry point.
void playNavigationHaptic();
