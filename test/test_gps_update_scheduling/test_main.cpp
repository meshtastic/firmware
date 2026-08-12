#include "Arduino.h"
#include "TestUtil.h"
#include "gps/GPSUpdateScheduling.h"
#include <cmath>
#include <cstdio>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

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
    exit(UNITY_END());
}

void loop() {}
