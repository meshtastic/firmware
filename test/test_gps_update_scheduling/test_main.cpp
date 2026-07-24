#include "gps/GPSUpdateScheduling.h"
#include <cmath>
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
    // Mix of table breakpoints and off-breakpoint values, capped at 900s (the pre-existing
    // 15-minute search clamp - see test_clamps_above_table_range for the boundary itself).
    const uint32_t samples[] = {1, 5, 10, 33, 60, 100, 150, 300, 500, 900};
    for (uint32_t s : samples) {
        double expected = originalFormula(s);
        uint32_t actual = gpsHardsleepThresholdMs(s);
        // Sub-10s inputs diverge more in relative (but small absolute) terms; see
        // GPSUpdateScheduling.cpp for the measured bounds.
        double tolerance = s < 10 ? 3000.0 : expected * 0.01 + 100.0;
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

static void test_clamps_above_table_range(void)
{
    uint32_t atMax = gpsHardsleepThresholdMs(900);
    TEST_ASSERT_EQUAL_UINT32(atMax, gpsHardsleepThresholdMs(2000));
    TEST_ASSERT_EQUAL_UINT32(atMax, gpsHardsleepThresholdMs(UINT32_MAX));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_matches_original_formula_at_sampled_points);
    RUN_TEST(test_zero_seconds_is_zero);
    RUN_TEST(test_monotonically_nondecreasing);
    RUN_TEST(test_clamps_above_table_range);
    return UNITY_END();
}
