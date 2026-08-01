#pragma once

#include <cstdint>
#ifdef PIO_UNIT_TESTING
#include <atomic>
#endif

// Monotonic uptime clock, injectable so tests can drive a virtual timebase instead of sleeping.
// Uptime only; see gps/RTC.h for wall-clock. Not named Time.h: -Isrc would shadow C's <time.h>.
namespace Time
{
#ifdef PIO_UNIT_TESTING
// Test-only virtual clock; OFF by default so suites relying on real time are unaffected. Atomic so
// a suite can step the clock from one thread while others read it - the concurrent-reader cases in
// test_uptime_clock/ do exactly that.
inline std::atomic<uint32_t> testNowMs{0};
inline std::atomic<bool> useTestClock{false};
using MonotonicPublishHook = void (*)();

inline void setTestMillis(uint32_t ms)
{
    testNowMs.store(ms, std::memory_order_relaxed);
    useTestClock.store(true, std::memory_order_relaxed);
}
inline void advanceTestMillis(uint32_t deltaMs)
{
    testNowMs.fetch_add(deltaMs, std::memory_order_relaxed);
    useTestClock.store(true, std::memory_order_relaxed);
}
// Restore real-clock behaviour (call in test tearDown if a suite mixes real and fake time).
inline void useRealClock()
{
    useTestClock.store(false, std::memory_order_relaxed);
    testNowMs.store(0, std::memory_order_relaxed);
}
// Zero the published wrap carry. Suites that assert absolute uptime values call this in setUp():
// a previous case that moved the test clock backwards left a counted wrap behind.
void resetMonotonicForTests();
void setMonotonicPublishHookForTests(MonotonicPublishHook hook);
#endif

/// Milliseconds since boot, 32-bit (wraps ~49.7 days). Drop-in for millis(). For "has this interval
/// elapsed / deadline arrived" use Throttle (isWithinTimespanMs / hasElapsed / deadlinePassed),
/// which is wrap-correct with no carry state at all.
uint32_t getMillis();

/// Milliseconds since boot as a monotonic 64-bit count.
///
/// A pure read: it derives its answer from a complete snapshot published by serviceMonotonic()
/// plus the unsigned elapsed time since that snapshot, which is exact across the wrap. A reader
/// that preempts publication uses the previous snapshot. If publication completes during a copy,
/// the reader retries; it never waits for a publish in progress.
///
/// Not intended for ISR call sites because lock-free std::atomic operations are not guaranteed by
/// every supported toolchain. ISRs use getMillis(); the publication protocol itself never waits.
uint64_t getMillisMonotonic();

/// Whole seconds since boot, derived from getMillisMonotonic() (~136 years of range). This is
/// the unit to store when an instant must be dated before the wall clock is trustworthy.
uint32_t getUptimeSecs();

/// Advances the published wrap carry. THE ONLY WRITER - call it from the main loop and nowhere
/// else. Two concurrent callers could count one wrap twice, jumping every uptime and wall-clock
/// reading ~49.7 days forward for the rest of the boot; keeping the write on one thread is what
/// makes that unrepresentable rather than merely unlikely.
///
/// Must run at least once per ~49.7-day wrap window. The main loop calls it every iteration, so
/// the real margin is the whole window rather than the instruction-wide race it replaces.
void serviceMonotonic();

} // namespace Time
