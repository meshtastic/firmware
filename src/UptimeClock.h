#pragma once

#include <cstdint>

// Monotonic uptime clock, injectable so tests can drive a virtual timebase instead of sleeping.
// Uptime only; see gps/RTC.h for wall-clock. Not named Time.h: -Isrc would shadow C's <time.h>.
namespace Time
{
#ifdef PIO_UNIT_TESTING
// Test-only virtual clock; OFF by default so suites relying on real time are unaffected.
inline uint32_t testNowMs = 0;
inline bool useTestClock = false;

inline void setTestMillis(uint32_t ms)
{
    testNowMs = ms;
    useTestClock = true;
}
inline void advanceTestMillis(uint32_t deltaMs)
{
    testNowMs += deltaMs;
    useTestClock = true;
}
// Restore real-clock behaviour (call in test tearDown if a suite mixes real and fake time).
inline void useRealClock()
{
    useTestClock = false;
    testNowMs = 0;
}
#endif

/// Milliseconds since boot, 32-bit (wraps ~49.7 days). Drop-in for millis().
///
/// There is deliberately no 64-bit variant. Code that needs to know whether an interval has elapsed
/// or a deadline has arrived should use Throttle (isWithinTimespanMs / hasElapsed / deadlinePassed),
/// which is correct across the wrap without carrying accumulator state that must be polled to stay
/// accurate - and which is ISR-safe, as a stateful 64-bit counter is not.
uint32_t getMillis();

} // namespace Time
