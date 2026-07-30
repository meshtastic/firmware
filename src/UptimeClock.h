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
// Zero getMillisMonotonic()'s wrap-carry state. Suites that assert absolute uptime values call
// this in setUp(): a previous case that moved the test clock backwards left a counted wrap
// behind. Production code never rebases the carry - a rebase on clock-set is exactly what made
// the old getMillis64() unable to observe a wrap under test.
void resetMonotonicForTests();
#endif

/// Milliseconds since boot, 32-bit (wraps ~49.7 days). Drop-in for millis(), and the only clock
/// read here that is safe from an ISR. For "has this interval elapsed / deadline arrived" use
/// Throttle (isWithinTimespanMs / hasElapsed / deadlinePassed), which is wrap-correct with no
/// carry state at all.
uint32_t getMillis();

/// Milliseconds since boot as a monotonic 64-bit count. Accumulates 32-bit wraps in static carry
/// state updated on every read, so it stays exact provided something reads it at least once per
/// 49.7-day window - guaranteed by construction: AirTime::runOnce() polls it every second via
/// getUptimeSecs(), and every timestamp path reads it far more often. Not ISR-safe (unguarded
/// mutable carry); ISRs use getMillis().
uint64_t getMillisMonotonic();

/// Whole seconds since boot, derived from getMillisMonotonic() (~136 years of range). This is
/// the unit to store when an instant must be dated before the wall clock is trustworthy.
uint32_t getUptimeSecs();

} // namespace Time
