// Unit tests for src/UptimeClock.{h,cpp} - the monotonic uptime seam.
// Covers: test-clock injection, stepping the injected clock, the real-clock fallback, and
// getMillisMonotonic()'s wrap counting. getMillis() itself is a plain 32-bit read with no wrap
// handling of its own - its consumers' wrap arithmetic is tested in test_throttle/.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include <cstdint>
#include <unity.h>

void setUp(void)
{
    Time::resetMonotonicForTests(); // absolute uptime assertions must not depend on case order
}
void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into other suites
}

// --- injection ---

void test_getMillis_returns_injected_value()
{
    Time::setTestMillis(123456);
    TEST_ASSERT_EQUAL_UINT32(123456, Time::getMillis());
}

void test_advanceTestMillis_steps_clock()
{
    Time::setTestMillis(1000);
    Time::advanceTestMillis(500);
    TEST_ASSERT_EQUAL_UINT32(1500, Time::getMillis());
}

// Advancing past 0xFFFFFFFF wraps like millis() does, rather than saturating. This is the property
// the Throttle wrap tests are built on, so it is worth pinning here too.
void test_advanceTestMillis_wraps_like_millis()
{
    Time::setTestMillis(0xFFFFFF00u);
    Time::advanceTestMillis(0x200u);
    TEST_ASSERT_EQUAL_UINT32(0x00000100u, Time::getMillis());
}

// --- getMillisMonotonic(): the wrap counter ---

void test_monotonic_matches_millis_before_any_wrap()
{
    Time::setTestMillis(123456);
    TEST_ASSERT_EQUAL_UINT64(123456u, Time::getMillisMonotonic());
}

void test_monotonic_counts_a_wrap()
{
    Time::setTestMillis(0xFFFFFF00u);
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFF00u, Time::getMillisMonotonic());

    Time::advanceTestMillis(0x200u); // crosses the 32-bit wrap; low word is now 0x00000100
    TEST_ASSERT_EQUAL_UINT64(0x100000100ull, Time::getMillisMonotonic());
}

void test_monotonic_counts_every_wrap_when_read_each_window()
{
    Time::setTestMillis(0x80000000u);
    TEST_ASSERT_EQUAL_UINT64(0x80000000ull, Time::getMillisMonotonic());

    // Three full 2^32 cycles, read once per half-cycle - well inside the required
    // one-read-per-49.7-days window.
    for (int wrap = 1; wrap <= 3; wrap++) {
        Time::advanceTestMillis(0x80000000u); // crosses the wrap; low word back to 0
        Time::getMillisMonotonic();
        Time::advanceTestMillis(0x80000000u); // completes the cycle; low word back to 0x80000000
        TEST_ASSERT_EQUAL_UINT64(0x80000000ull + ((uint64_t)wrap << 32), Time::getMillisMonotonic());
    }
}

// The documented contract, pinned: a full 2^32 ms elapsing between two reads is indistinguishable
// from no time passing, so the wrap is missed. This is why the guaranteed poll (AirTime's 1s
// runOnce()) matters - and why the accessor must never be read "only rarely" by construction.
void test_monotonic_misses_a_wrap_not_read_within_the_window()
{
    Time::setTestMillis(1000);
    TEST_ASSERT_EQUAL_UINT64(1000u, Time::getMillisMonotonic());

    Time::advanceTestMillis(0x80000000u);
    Time::advanceTestMillis(0x80000000u); // full cycle with no read in between: low word is 1000 again

    TEST_ASSERT_EQUAL_UINT64(1000u, Time::getMillisMonotonic()); // the elapsed 2^32 ms is lost
}

void test_getUptimeSecs_stays_exact_across_the_wrap()
{
    Time::setTestMillis(4294967000u); // 4294967 whole seconds, 296ms short of the wrap
    TEST_ASSERT_EQUAL_UINT32(4294967u, Time::getUptimeSecs());

    Time::advanceTestMillis(1000); // crosses the wrap
    TEST_ASSERT_EQUAL_UINT32(4294968u, Time::getUptimeSecs());
}

// --- real clock fallback ---

void test_real_clock_advances_when_not_injected()
{
    Time::useRealClock();
    uint32_t t0 = Time::getMillis();
    testDelay(5);
    uint32_t t1 = Time::getMillis();
    TEST_ASSERT_TRUE(t1 >= t0); // real millis() is monotonic over a short delay
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_getMillis_returns_injected_value);
    RUN_TEST(test_advanceTestMillis_steps_clock);
    RUN_TEST(test_advanceTestMillis_wraps_like_millis);
    RUN_TEST(test_monotonic_matches_millis_before_any_wrap);
    RUN_TEST(test_monotonic_counts_a_wrap);
    RUN_TEST(test_monotonic_counts_every_wrap_when_read_each_window);
    RUN_TEST(test_monotonic_misses_a_wrap_not_read_within_the_window);
    RUN_TEST(test_getUptimeSecs_stays_exact_across_the_wrap);
    RUN_TEST(test_real_clock_advances_when_not_injected);
    exit(UNITY_END());
}

void loop() {}
