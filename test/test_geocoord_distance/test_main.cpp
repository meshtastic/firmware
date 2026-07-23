#include "gps/GeoCoord.h"
#include <cmath>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// GeoCoord::latLongToMeter() was rewritten from the exact spherical law of cosines (needing
// cos()/sin()/acos(), ~4.4KB of libm on wio-e5) to an equirectangular (flat-plane) approximation,
// to avoid linking that trig chain for what is a display/movement-threshold utility. These tests
// pin the measured accuracy bounds against the original formula, so a future change can't silently
// regress them.

static double referenceSphericalLawOfCosines(double lat_a, double lng_a, double lat_b, double lng_b)
{
    double a1 = lat_a * M_PI / 180.0;
    double a2 = lng_a * M_PI / 180.0;
    double b1 = lat_b * M_PI / 180.0;
    double b2 = lng_b * M_PI / 180.0;
    double t1 = cos(a1) * cos(a2) * cos(b1) * cos(b2);
    double t2 = cos(a1) * sin(a2) * cos(b1) * sin(b2);
    double t3 = sin(a1) * sin(b1);
    double arg = t1 + t2 + t3;
    if (arg > 1.0)
        arg = 1.0;
    if (arg < -1.0)
        arg = -1.0;
    return 6366000 * acos(arg);
}

static void assertWithinPercent(double expected, double actual, double pct, const char *msg)
{
    if (expected < 1.0)
        return; // near-zero distances: relative error is meaningless, skip
    double err = fabs(actual - expected) / expected * 100.0;
    if (err > pct) {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s: expected=%.1f actual=%.1f err=%.2f%% (limit %.2f%%)", msg, expected, actual, err, pct);
        TEST_FAIL_MESSAGE(buf);
    }
}

static void test_identical_points_is_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, GeoCoord::latLongToMeter(51.5, -0.1, 51.5, -0.1));
}

static void test_local_distances_within_1_percent(void)
{
    // Movement-threshold scale (meters to a few km) - the most common real usage
    // (PositionModule's smart-broadcast check, PositionsApplet/FavoritesMapApplet's 10m/50m
    // thresholds).
    struct {
        double la, lo, lb, lob;
    } cases[] = {
        {51.5074, -0.1278, 51.5080, -0.1278}, // ~67m north
        {51.5074, -0.1278, 51.5074, -0.1200}, // ~540m east at London's latitude
        {0.0, 0.0, 0.001, 0.001},             // ~157m near the equator
        {65.0, 25.0, 65.001, 25.002},         // high-ish latitude, small delta
        {-33.87, 151.21, -33.865, 151.215},   // Sydney, southern hemisphere
    };
    for (auto &c : cases) {
        double expected = referenceSphericalLawOfCosines(c.la, c.lo, c.lb, c.lob);
        double actual = GeoCoord::latLongToMeter(c.la, c.lo, c.lb, c.lob);
        assertWithinPercent(expected, actual, 1.0, "local distance");
    }
}

static void test_regional_distances_within_4_percent(void)
{
    // City-to-city scale (tens to ~500km), non-extreme latitudes
    struct {
        double la, lo, lb, lob;
    } cases[] = {
        {51.5074, -0.1278, 48.8566, 2.3522},      // London to Paris, ~344km
        {40.7128, -74.0060, 42.3601, -71.0589},   // NYC to Boston, ~306km
        {35.6762, 139.6503, 34.6937, 135.5023},   // Tokyo to Osaka, ~400km
        {-33.8688, 151.2093, -37.8136, 144.9631}, // Sydney to Melbourne, ~714km
    };
    for (auto &c : cases) {
        double expected = referenceSphericalLawOfCosines(c.la, c.lo, c.lb, c.lob);
        double actual = GeoCoord::latLongToMeter(c.la, c.lo, c.lb, c.lob);
        assertWithinPercent(expected, actual, 4.0, "regional distance");
    }
}

static void test_antimeridian_wraparound(void)
{
    // Two points ~22km apart straddling the 180th meridian. A naive longitude subtraction
    // (b2-a2 without wrapping) would compute this as ~40,000km - this is the regression case for
    // the wraparound fix.
    double expected = referenceSphericalLawOfCosines(0.0, 179.9, 0.0, -179.9);
    double actual = GeoCoord::latLongToMeter(0.0, 179.9, 0.0, -179.9);
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.05f, expected, actual);
    TEST_ASSERT_LESS_THAN_FLOAT(1000000.0f, actual); // sanity: nowhere near the naive-bug's ~40,000km
}

static void test_symmetry(void)
{
    // distance(a,b) should equal distance(b,a)
    double d1 = GeoCoord::latLongToMeter(51.5074, -0.1278, 48.8566, 2.3522);
    double d2 = GeoCoord::latLongToMeter(48.8566, 2.3522, 51.5074, -0.1278);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, d1, d2);
}

static void test_no_nan_at_extreme_latitudes(void)
{
    float d1 = GeoCoord::latLongToMeter(90.0, 0.0, -90.0, 0.0);
    float d2 = GeoCoord::latLongToMeter(89.9, 10.0, 89.9, -170.0);
    float d3 = GeoCoord::latLongToMeter(-89.9, 45.0, -89.9, -135.0);
    TEST_ASSERT_FALSE(std::isnan(d1));
    TEST_ASSERT_FALSE(std::isnan(d2));
    TEST_ASSERT_FALSE(std::isnan(d3));
    TEST_ASSERT_TRUE(d1 > 0);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_identical_points_is_zero);
    RUN_TEST(test_local_distances_within_1_percent);
    RUN_TEST(test_regional_distances_within_4_percent);
    RUN_TEST(test_antimeridian_wraparound);
    RUN_TEST(test_symmetry);
    RUN_TEST(test_no_nan_at_extreme_latitudes);
    return UNITY_END();
}
