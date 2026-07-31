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
inline bool clockSourceChanged = true; // forces getMillis64() to rebase its wrap accumulator

inline void setTestMillis(uint32_t ms)
{
    testNowMs = ms;
    useTestClock = true;
    clockSourceChanged = true;
}
inline void advanceTestMillis(uint32_t deltaMs)
{
    // Advancing from 0 after getMillis64() sampled the real clock steps backward, which would
    // otherwise be miscounted as a wrap.
    if (!useTestClock)
        clockSourceChanged = true;
    testNowMs += deltaMs;
    useTestClock = true;
}
// Restore real-clock behaviour (call in test tearDown if a suite mixes real and fake time).
inline void useRealClock()
{
    useTestClock = false;
    testNowMs = 0;
    clockSourceChanged = true;
}
#endif

/// Milliseconds since boot, 32-bit (wraps ~49.7 days). Drop-in for millis().
uint32_t getMillis();

/// Milliseconds since boot, 64-bit, rollover-immune. Must be polled at least once per ~49.7-day
/// wrap window to catch every wrap, and keeps mutable static carry state, so it is NOT ISR-safe.
uint64_t getMillis64();

} // namespace Time
