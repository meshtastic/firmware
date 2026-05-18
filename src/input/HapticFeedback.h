#pragma once

#include "configuration.h"

#ifdef HAPTIC_FEEDBACK_PIN

#include "concurrency/OSThread.h"
#include <stdint.h>

// Drives short, non-blocking pulses on a GPIO-controlled vibration motor.
// A variant opts in by defining HAPTIC_FEEDBACK_PIN; HAPTIC_FEEDBACK_ACTIVE_LOW
// inverts the drive polarity (default: active-high — pin HIGH = motor on).
//
// Used by the touch button to produce button-like haptic feedback. Coexists
// with ExternalNotificationModule if both target the same pin — pulses are
// fire-and-forget, no synchronization, last writer wins.
class HapticFeedback : public concurrency::OSThread
{
  public:
    HapticFeedback();

    // Turn motor on now, schedule off after durationMs.
    void pulse(uint16_t durationMs = 30);

    // Schedule a one-shot pulse to fire delayMs from now.
    void armDelayedPulse(uint16_t delayMs, uint16_t durationMs = 30);

    // Cancel a previously-armed delayed pulse (no effect if none pending).
    void cancelDelayedPulse();

  protected:
    int32_t runOnce() override;

  private:
    uint32_t pulseOffAt = 0;     // millis() when current pulse should end (0 = no pulse active)
    uint32_t delayedPulseAt = 0; // millis() when armed pulse should fire (0 = nothing armed)
    uint16_t delayedPulseDuration = 0;

    void motorWrite(bool on);
};

extern HapticFeedback *hapticFeedback;

// Lazy-instantiate the global on first call. Safe to call repeatedly.
void initHapticFeedback();

#endif // HAPTIC_FEEDBACK_PIN
