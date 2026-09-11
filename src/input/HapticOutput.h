#pragma once

#include <cstdint>
#include <climits>

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

// Hardware-specific motor control. Scheduling, preferences, and input mapping
// remain in HapticFeedback.
class HapticOutput
{
  public:
    virtual ~HapticOutput() = default;

    virtual bool begin() { return false; }
    virtual bool isAvailable() const { return false; }
    virtual void play(HapticEffect effect) { (void)effect; }
    virtual void stop() {}
    virtual void pulse(uint16_t durationMs) { (void)durationMs; }
    virtual void armDelayedPulse(uint16_t delayMs, uint16_t durationMs)
    {
        (void)delayMs;
        (void)durationMs;
    }
    virtual void cancelDelayedPulse() {}
    virtual int32_t runOnce() { return INT_MAX; }
};
