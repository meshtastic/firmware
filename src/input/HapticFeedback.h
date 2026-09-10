#pragma once

#include "configuration.h"

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
