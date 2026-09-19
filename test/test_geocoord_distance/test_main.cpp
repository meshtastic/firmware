// Deliberately does NOT include TestUtil.h. This suite is pure-function - no NodeDB, no router, no
// sockets, no PKC - so the harness-wide guards there (no listening sockets, force_simradio clear)
// would assert conditions it cannot reach, and initializeTestEnvironment()'s RTC and OSThread setup
// would add portduino globals it otherwise never touches. Suite-level state cleanliness is still
// checked from outside by bin/pio-test-isolate.sh, which wraps every suite regardless.
#include "configuration.h"
#include "gps/GeoCoord.h"
#include <cmath>
#include <cstdio>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// Pins latLongToMeter()'s equirectangular-approximation accuracy against the original spherical
// law of cosines, so a future change can't silently regress it.

static constexpr double kPi = 3.14159265358979323846;

static double referenceSphericalLawOfCosines(double lat_a, double lng_a, double lat_b, double lng_b)
{
    double a1 = lat_a * kPi / 180.0;
    double a2 = lng_a * kPi / 180.0;
    double b1 = lat_b * kPi / 180.0;
    double b2 = lng_b * kPi / 180.0;
    double t1 = std::cos(a1) * std::cos(a2) * std::cos(b1) * std::cos(b2);
    double t2 = std::cos(a1) * std::sin(a2) * std::cos(b1) * std::sin(b2);
    double t3 = std::sin(a1) * std::sin(b1);
    double arg = t1 + t2 + t3;
    if (arg > 1.0)
        arg = 1.0;
    if (arg < -1.0)
        arg = -1.0;
    return 6366000 * std::acos(arg);
}

// Below ~1m, relative error is dominated by rounding noise rather than the formula itself, so
// assert an absolute bound instead (still catches a badly-broken implementation).
static constexpr double kNearZeroAbsoluteToleranceMeters = 0.5;

// An order of magnitude above what the implementation currently produces per group - tight enough to
// catch a regression, loose enough not to track float rounding. Groups differ because
// equirectangular error grows with both separation and latitude.
static constexpr double kLocalTolerancePercent = 0.01;
static constexpr double kRegionalTolerancePercent = 0.1;
static constexpr double kHighLatitudeTolerancePercent = 0.2;
static constexpr double kAntimeridianTolerancePercent = 0.01;

static void assertWithinPercent(double expected, double actual, double pct, const char *msg)
{
    if (expected < 1.0) {
        if (std::fabs(actual - expected) > kNearZeroAbsoluteToleranceMeters) {
            char buf[160];
            snprintf(buf, sizeof(buf), "%s: expected=%.3f actual=%.3f (near-zero, limit %.1fm absolute)", msg, expected, actual,
                     kNearZeroAbsoluteToleranceMeters);
            TEST_FAIL_MESSAGE(buf);
        }
        return;
    }
    double err = std::fabs(actual - expected) / expected * 100.0;
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

static void test_local_distances(void)
{
    // Movement-threshold scale (meters to a few km) - the most common real usage.
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
        assertWithinPercent(expected, actual, kLocalTolerancePercent, "local distance");
    }
}

static void test_regional_distances(void)
{
    // City-to-city scale (tens to ~500km) below 60 degrees; see test_high_latitude_distances.
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
        assertWithinPercent(expected, actual, kRegionalTolerancePercent, "regional distance");
    }
}

static void test_high_latitude_distances(void)
{
    // Regional scale above 60 degrees, where equirectangular error grows fastest - a 500km pair at
    // 80 degrees already exceeds 1%.
    struct {
        double la, lo, lb, lob;
    } cases[] = {
        {69.6492, 18.9553, 67.2804, 14.4049},     // Tromso to Bodo, ~322km
        {64.8378, -147.7164, 61.2181, -149.9003}, // Fairbanks to Anchorage, ~417km
        {78.2232, 15.6469, 78.9230, 11.9219},     // Longyearbyen to Ny-Alesund, ~113km
    };
    for (auto &c : cases) {
        double expected = referenceSphericalLawOfCosines(c.la, c.lo, c.lb, c.lob);
        double actual = GeoCoord::latLongToMeter(c.la, c.lo, c.lb, c.lob);
        assertWithinPercent(expected, actual, kHighLatitudeTolerancePercent, "high-latitude distance");
    }
}

static void test_antimeridian_wraparound(void)
{
    // Two points ~22km apart straddling the 180th meridian - regression case for the antimeridian
    // wraparound fix (a naive b2-a2 would compute this as ~40,000km).
    double expected = referenceSphericalLawOfCosines(0.0, 179.9, 0.0, -179.9);
    double actual = GeoCoord::latLongToMeter(0.0, 179.9, 0.0, -179.9);
    assertWithinPercent(expected, actual, kAntimeridianTolerancePercent, "antimeridian distance");
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

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_identical_points_is_zero);
    RUN_TEST(test_local_distances);
    RUN_TEST(test_regional_distances);
    RUN_TEST(test_high_latitude_distances);
    RUN_TEST(test_antimeridian_wraparound);
    RUN_TEST(test_symmetry);
    RUN_TEST(test_no_nan_at_extreme_latitudes);
    exit(UNITY_END());
}

void loop() {}
