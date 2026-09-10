#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "gps/GPSUpdateScheduling.h"
#include <cmath>
#include <cstdio>
#include <unity.h>

void setUp(void)
{
    Time::setTestMillis(0);
}
void tearDown(void)
{
    Time::useRealClock();
}

// Confirms gpsHardsleepThresholdMs()'s pow()-free lookup table tracks the original
// `2750 * pow(seconds, 1.22)` curve closely.
static double originalFormula(uint32_t seconds)
{
    return 2750.0 * std::pow((double)seconds, 1.22);
}

static void test_matches_original_formula_at_sampled_points(void)
{
    // Off-breakpoint values only - a breakpoint interpolates exactly by construction, so it would
    // test nothing here (test_exact_at_table_breakpoints covers those). Includes both worst-error
    // inputs: 7s (1.60%) and 728s (0.55%). Capped at 900s, the pre-existing 15-minute search clamp.
    const uint32_t samples[] = {4, 6, 7, 8, 9, 33, 100, 150, 500, 728, 899};
    for (uint32_t s : samples) {
        double expected = originalFormula(s);
        uint32_t actual = gpsHardsleepThresholdMs(s);
        // Pure integer arithmetic, so results are bit-identical everywhere - no float noise to
        // leave headroom for, and these sit just above the measured worst cases.
        double tolerance = expected * (s < 10 ? 0.02 : 0.0075);
        TEST_ASSERT_DOUBLE_WITHIN(tolerance, expected, (double)actual);
    }
}

static void test_zero_seconds_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, gpsHardsleepThresholdMs(0));
}

static void test_monotonically_nondecreasing(void)
{
    uint32_t prev = gpsHardsleepThresholdMs(0);
    for (uint32_t s = 1; s <= 1200; s += 7) {
        uint32_t cur = gpsHardsleepThresholdMs(s);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(prev, cur);
        prev = cur;
    }
}

static void test_exact_at_table_breakpoints(void)
{
    // Every breakpoint must return its own sampled value. Catches an off-by-one in the segment
    // scan, which a percentage bound on interpolated points would absorb.
    const uint32_t breakpoints[] = {0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 300, 450, 600, 900};
    for (uint32_t s : breakpoints) {
        char msg[64];
        snprintf(msg, sizeof(msg), "breakpoint %us", s);
        // Within 2ms, not exact: the 30s entry is rounded 1ms high, and pow() can differ by an ULP
        // across libm implementations. A real off-by-one in the scan misses by thousands.
        TEST_ASSERT_UINT32_WITHIN_MESSAGE(2, (uint32_t)(originalFormula(s) + 0.5), gpsHardsleepThresholdMs(s), msg);
    }
}

static void test_clamps_above_table_range(void)
{
    uint32_t atMax = gpsHardsleepThresholdMs(900);
    TEST_ASSERT_EQUAL_UINT32(atMax, gpsHardsleepThresholdMs(2000));
    TEST_ASSERT_EQUAL_UINT32(atMax, gpsHardsleepThresholdMs(UINT32_MAX));
}

static void test_clamp_boundary(void)
{
    // The clamp must engage exactly at the last table point, not before or after it.
    TEST_ASSERT_LESS_THAN_UINT32(gpsHardsleepThresholdMs(900), gpsHardsleepThresholdMs(899));
    TEST_ASSERT_EQUAL_UINT32(gpsHardsleepThresholdMs(900), gpsHardsleepThresholdMs(901));
}

// elapsedSearchMs() across the 32-bit millis() wrap. Ordering the two raw stamps, as it used to,
// reports an idle receiver as searching or a searching one as idle, and searchedTooLong() acts on it.

// A search that has not started yet reads as idle, not as a search of length millis().
static void test_elapsed_is_zero_before_any_search(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(90 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, s.elapsedSearchMs());
}

static void test_elapsed_tracks_the_clock_while_searching(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(10 * 1000);
    s.informSearching();
    Time::advanceTestMillis(7 * 1000);
    TEST_ASSERT_EQUAL_UINT32(7 * 1000, s.elapsedSearchMs());
}

static void test_elapsed_is_zero_once_the_search_ends(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(10 * 1000);
    s.informSearching();
    Time::advanceTestMillis(7 * 1000);
    s.informGotLock();
    Time::advanceTestMillis(60 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, s.elapsedSearchMs());

    s.informSearching();
    Time::advanceTestMillis(3 * 1000);
    s.informSearchFailed();
    TEST_ASSERT_EQUAL_UINT32(0, s.elapsedSearchMs());
}

// Start before the wrap, still searching after it: elapsed must be the real 10s, not ~49.7 days.
static void test_elapsed_is_exact_across_the_wrap(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(0xFFFFF000u);
    s.informSearching();
    Time::advanceTestMillis(0x1000u + 6 * 1000); // 4.096s to the wrap, then 6s past it
    TEST_ASSERT_EQUAL_UINT32(0x1000u + 6 * 1000, s.elapsedSearchMs());
}

// The regression: started before the wrap, ended after it, so searchStartedMs > searchEndedMs.
// The receiver is idle and elapsed must say so.
static void test_search_ending_after_the_wrap_reads_as_idle(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(0xFFFFF000u);
    s.informSearching();
    Time::advanceTestMillis(0x1000u + 2 * 1000);
    s.informGotLock();
    // The stamps really are inverted: the search ended at a smaller millis() than it started at.
    TEST_ASSERT_LESS_THAN_UINT32(0xFFFFF000u, Time::getMillis());
    Time::advanceTestMillis(30 * 60 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, s.elapsedSearchMs());
}

// The mirror image: the previous search ended before the wrap, this one started after it, so
// searchStartedMs < searchEndedMs while a search is genuinely in progress.
static void test_search_starting_after_the_wrap_reads_as_searching(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(0xFFFFF000u);
    s.informSearching();
    Time::advanceTestMillis(1000);
    s.informGotLock();
    Time::advanceTestMillis(0x1000u); // over the wrap
    s.informSearching();
    Time::advanceTestMillis(12 * 1000);
    TEST_ASSERT_EQUAL_UINT32(12 * 1000, s.elapsedSearchMs());
}

static void test_reset_clears_the_search_state(void)
{
    GPSUpdateScheduling s;
    Time::setTestMillis(10 * 1000);
    s.informSearching();
    Time::advanceTestMillis(5 * 1000);
    s.reset();
    TEST_ASSERT_EQUAL_UINT32(0, s.elapsedSearchMs());
}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_matches_original_formula_at_sampled_points);
    RUN_TEST(test_zero_seconds_is_zero);
    RUN_TEST(test_monotonically_nondecreasing);
    RUN_TEST(test_exact_at_table_breakpoints);
    RUN_TEST(test_clamps_above_table_range);
    RUN_TEST(test_clamp_boundary);
    RUN_TEST(test_elapsed_is_zero_before_any_search);
    RUN_TEST(test_elapsed_tracks_the_clock_while_searching);
    RUN_TEST(test_elapsed_is_zero_once_the_search_ends);
    RUN_TEST(test_elapsed_is_exact_across_the_wrap);
    RUN_TEST(test_search_ending_after_the_wrap_reads_as_idle);
    RUN_TEST(test_search_starting_after_the_wrap_reads_as_searching);
    RUN_TEST(test_reset_clears_the_search_state);
    exit(UNITY_END());
}

void loop() {}
