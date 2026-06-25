#include "HapticFeedback.h"

#ifdef HAPTIC_FEEDBACK_PIN

#include <Arduino.h>

#ifdef HAPTIC_FEEDBACK_ACTIVE_LOW
#define HAPTIC_FEEDBACK_ON_STATE LOW
#define HAPTIC_FEEDBACK_OFF_STATE HIGH
#else
#define HAPTIC_FEEDBACK_ON_STATE HIGH
#define HAPTIC_FEEDBACK_OFF_STATE LOW
#endif

HapticFeedback *hapticFeedback = nullptr;

void initHapticFeedback()
{
    if (!hapticFeedback)
        hapticFeedback = new HapticFeedback();
}

HapticFeedback::HapticFeedback() : concurrency::OSThread("Haptic")
{
    pinMode(HAPTIC_FEEDBACK_PIN, OUTPUT);
    digitalWrite(HAPTIC_FEEDBACK_PIN, HAPTIC_FEEDBACK_OFF_STATE);
}

void HapticFeedback::motorWrite(bool on)
{
    digitalWrite(HAPTIC_FEEDBACK_PIN, on ? HAPTIC_FEEDBACK_ON_STATE : HAPTIC_FEEDBACK_OFF_STATE);
}

void HapticFeedback::pulse(uint16_t durationMs)
{
    motorWrite(true);
    pulseOffAt = millis() + durationMs;
    if (pulseOffAt == 0) // 0 is the "no pulse" sentinel
        pulseOffAt = 1;
    scheduleNext();
}

void HapticFeedback::armDelayedPulse(uint16_t delayMs, uint16_t durationMs)
{
    delayedPulseAt = millis() + delayMs;
    if (delayedPulseAt == 0)
        delayedPulseAt = 1;
    delayedPulseDuration = durationMs;
    scheduleNext();
}

void HapticFeedback::cancelDelayedPulse()
{
    delayedPulseAt = 0;
}

void HapticFeedback::scheduleNext()
{
    uint32_t now = millis();
    uint32_t next = 0;
    if (pulseOffAt != 0)
        next = pulseOffAt;
    if (delayedPulseAt != 0 && (next == 0 || (int32_t)(delayedPulseAt - next) < 0))
        next = delayedPulseAt;
    if (next == 0)
        return;
    int32_t delay = (int32_t)(next - now);
    setIntervalFromNow(delay > 0 ? (unsigned long)delay : 0);
}

int32_t HapticFeedback::runOnce()
{
    uint32_t now = millis();

    if (pulseOffAt != 0 && (int32_t)(now - pulseOffAt) >= 0) {
        motorWrite(false);
        pulseOffAt = 0;
    }

    if (delayedPulseAt != 0 && (int32_t)(now - delayedPulseAt) >= 0) {
        uint16_t dur = delayedPulseDuration;
        delayedPulseAt = 0;
        pulse(dur);
    }

    uint32_t next = 0;
    if (pulseOffAt != 0)
        next = pulseOffAt;
    if (delayedPulseAt != 0 && (next == 0 || (int32_t)(delayedPulseAt - next) < 0))
        next = delayedPulseAt;
    if (next == 0)
        return 60 * 1000;
    int32_t delay = (int32_t)(next - now);
    return delay > 0 ? delay : 0;
}

#endif // HAPTIC_FEEDBACK_PIN
