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

/// Step a millis value past 0. Stored stamps and deadlines conventionally use 0 for "unset", so the
/// one tick per ~49.7-day wrap that lands on 0 would read as never-set; 1 is a 1 ms error instead.
constexpr uint32_t skipZero(uint32_t ms)
{
    return ms ? ms : 1;
}

/// Start a countdown to delayMs from now, never 0. The sum is what has to dodge 0 - a non-zero
/// read plus a delay lands there once per wrap - so this is not skipZero(getMillis()) + delayMs.
inline uint32_t timerEndsAtMillis(uint32_t delayMs)
{
    return skipZero(getMillis() + delayMs);
}

/// The clock read for a site that works with 0-means-unset stamps: getMillis() with the one 0 tick
/// called 1.
///
/// Use this at the READ, not skipZero() at the store, whenever the same value is both stored as a
/// stamp and used to measure elapsed time against stamps. Applying skipZero() only at the store
/// splits the two sides apart for one tick per wrap: the stamp becomes 1 while a raw `now` is still
/// 0, so `now - stamp` is UINT32_MAX - the stamp reads as ~49.7 days old instead of brand new, and
/// an elapsed-since guard (a cooldown, a debounce, a long-press threshold) fires when it should not.
/// Reading through this keeps both sides on one value, so that tick is simply called tick 1 and the
/// elapsed time comes out 0. The cost is the same 1 ms skew skipZero() already documents.
inline uint32_t stampMillis()
{
    return skipZero(getMillis());
}

// skipZero() is the whole 0-means-unset contract in one expression, and it is constexpr, so pin it
// here rather than only in test_uptime_clock: a build that breaks it stops at this header instead of
// shipping a deadline that reads as never-set. The two obvious "simplifications" are what these
// catch - `ms | 1` perturbs every even value, and `ms + 1` turns the last tick of the wrap into the
// 0 the function exists to avoid. Both compile and both pass a test that only checks skipZero(0).
static_assert(skipZero(0) == 1, "skipZero must lift the one 0 tick to 1");
static_assert(skipZero(1) == 1, "skipZero must leave 1 alone");
static_assert(skipZero(2) == 2, "skipZero must pass even values through untouched (ms | 1 would not)");
static_assert(skipZero(UINT32_MAX) == UINT32_MAX, "skipZero must not wrap the last tick to 0 (ms + 1 would)");

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
/// reading ~49.7 days forward for the rest of the boot.
///
/// Must run at least once per ~49.7-day wrap window; the main loop calls it every iteration.
void serviceMonotonic();

} // namespace Time
