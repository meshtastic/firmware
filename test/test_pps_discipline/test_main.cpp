// Unit tests for PpsDiscipline in src/gps/PpsDiscipline.cpp - how GPS::processPps() turns PPS edges into clock
// corrections and a frequency estimate.
//
// A PPS edge marks the top of a UTC second but not which second. The NMEA-set clock names it: the nearest second,
// which is only unambiguous while the clock is within half a second. An edge is used only while the receiver has a
// fix, because before one the receiver pulses on its own unaligned clock (a u-blox M10 does), and only when it
// follows the previous edge by a whole number of seconds, so one glitch on the line cannot move the clock. The IRQ
// is armed in short bursts, so the frequency estimate is taken against an anchor edge that survives between bursts.
//
// Regressions guarded:
//  - accepting an edge without a fix, or a lone edge, which pins the clock to a pulse that is not on a UTC second;
//  - picking the wrong second when the NMEA clock lags or leads, which puts the clock out by a whole second;
//  - accepting an edge whose spacing from the last is off by more than crystal error (a glitch or a missed edge
//    taken for another), which moves the clock by the glitch;
//  - a rate sign error, which doubles drift instead of cancelling it, or losing the anchor between bursts, which
//    leaves the rate measured over seconds instead of minutes.
#include "Arduino.h"
#include "TestUtil.h"
#include "gps/PpsDiscipline.h"
#include <cstdlib>
#include <stdint.h>
#include <unity.h>

static constexpr uint64_t kSecond = 1000000;
static constexpr uint64_t kUtc0 = 1800000000ULL * kSecond; // a UTC second boundary
static constexpr uint64_t kMono0 = 5 * kSecond;            // monotonic µs at that boundary

void setUp(void) {}
void tearDown(void) {}

// A receiver locked to UTC, seen through a monotonic clock that gains ppb ns per second, and a wall clock that
// reads clockErrorUs off true UTC at every edge.
struct Edge {
    uint64_t mono;
    uint64_t clock;
};
static Edge edgeAt(uint64_t second, int64_t ppb, int64_t clockErrorUs)
{
    const uint64_t elapsedUs = second * kSecond;
    return {kMono0 + elapsedUs + (uint64_t)((int64_t)elapsedUs / 1000 * ppb / 1000000), kUtc0 + elapsedUs + clockErrorUs};
}

static void test_no_fix_is_never_used(void)
{
    PpsDiscipline d;
    for (uint64_t s = 0; s < 5; s++) {
        const Edge e = edgeAt(s, 0, 0);
        TEST_ASSERT_FALSE(d.onEdge(e.mono, e.clock, false).accepted);
    }
}

// The first edge has nothing to be checked against.
static void test_lone_edge_is_not_used(void)
{
    PpsDiscipline d;
    const Edge e0 = edgeAt(0, 0, -120000);
    TEST_ASSERT_FALSE(d.onEdge(e0.mono, e0.clock, true).accepted);
    const Edge e1 = edgeAt(1, 0, -120000);
    const PpsEdgeResult r = d.onEdge(e1.mono, e1.clock, true);
    TEST_ASSERT_TRUE(r.accepted);
    TEST_ASSERT_EQUAL_UINT64(kUtc0 + kSecond, r.utcUs);
    TEST_ASSERT_EQUAL_INT64(-120000, r.errorUs);
}

// NMEA clocks lag by the module's latency; one that leads slightly (after a PPS pin and drift) must still round
// to the same second.
static void test_nearest_second_is_chosen(void)
{
    const int64_t errors[] = {-399000, -49000, 0, 30, 2500, 399000};
    for (int64_t err : errors) {
        PpsDiscipline d;
        const Edge e0 = edgeAt(0, 0, err), e1 = edgeAt(1, 0, err);
        d.onEdge(e0.mono, e0.clock, true);
        const PpsEdgeResult r = d.onEdge(e1.mono, e1.clock, true);
        TEST_ASSERT_TRUE(r.accepted);
        TEST_ASSERT_EQUAL_UINT64(kUtc0 + kSecond, r.utcUs);
        TEST_ASSERT_EQUAL_INT64(err, r.errorUs);
    }
}

// Past MAX_CLOCK_ERROR_US the nearest second may be the wrong one.
static void test_clock_too_far_is_not_used(void)
{
    PpsDiscipline d;
    const Edge e0 = edgeAt(0, 0, -450000), e1 = edgeAt(1, 0, -450000);
    d.onEdge(e0.mono, e0.clock, true);
    TEST_ASSERT_FALSE(d.onEdge(e1.mono, e1.clock, true).accepted);
}

// A glitch 300 ms after a real edge is not a second later; it must not be used, and the next real edge, which
// does not follow the glitch by a whole second either, must wait for a consistent successor.
static void test_glitch_is_rejected(void)
{
    PpsDiscipline d;
    Edge e0 = edgeAt(0, 0, 0), e1 = edgeAt(1, 0, 0);
    d.onEdge(e0.mono, e0.clock, true);
    TEST_ASSERT_TRUE(d.onEdge(e1.mono, e1.clock, true).accepted);
    // Glitch: the clock reads 300 ms past the second, so it rounds to the same second as e1 - not later
    TEST_ASSERT_FALSE(d.onEdge(e1.mono + 300000, e1.clock + 300000, true).accepted);
    const Edge e2 = edgeAt(2, 0, 0), e3 = edgeAt(3, 0, 0);
    TEST_ASSERT_FALSE(d.onEdge(e2.mono, e2.clock, true).accepted);
    TEST_ASSERT_TRUE(d.onEdge(e3.mono, e3.clock, true).accepted);
}

// A missed edge leaves a two-second gap, which is still whole seconds and is used.
static void test_missed_edge_is_tolerated(void)
{
    PpsDiscipline d;
    const Edge e0 = edgeAt(0, 30000, 0), e2 = edgeAt(2, 30000, 0);
    d.onEdge(e0.mono, e0.clock, true);
    TEST_ASSERT_TRUE(d.onEdge(e2.mono, e2.clock, true).accepted);
}

// Spacing off by more than INTERVAL_TOLERANCE_PPM (here 5 ms in one second) is not crystal error.
static void test_spacing_beyond_tolerance_is_rejected(void)
{
    PpsDiscipline d;
    const Edge e0 = edgeAt(0, 0, 0), e1 = edgeAt(1, 0, 0);
    d.onEdge(e0.mono, e0.clock, true);
    TEST_ASSERT_FALSE(d.onEdge(e1.mono + 5000, e1.clock, true).accepted);
}

// A monotonic clock 30 ppm fast gains 30000 ns per second; the estimate must say so, with that sign.
static void test_rate_is_measured_with_the_right_sign(void)
{
    PpsDiscipline d;
    for (uint64_t s = 0; s <= 20; s++) {
        const Edge e = edgeAt(s, 30000, 0);
        d.onEdge(e.mono, e.clock, true);
    }
    TEST_ASSERT_TRUE(d.hasRate());
    TEST_ASSERT_INT32_WITHIN(100, 30000, d.ratePpb());
}

// No rate from a baseline shorter than MIN_RATE_SPAN_US: µs timestamp noise over seconds is tens of ppm.
static void test_no_rate_from_a_short_baseline(void)
{
    PpsDiscipline d;
    for (uint64_t s = 0; s <= 5; s++) {
        const Edge e = edgeAt(s, 30000, 0);
        d.onEdge(e.mono, e.clock, true);
    }
    TEST_ASSERT_FALSE(d.hasRate());
}

// Bursts of a few edges ten minutes apart: the rate comes from the anchor in the first burst, and is fine-grained
// because the baseline is long. reset() between bursts must not drop the anchor.
static void test_rate_spans_bursts(void)
{
    PpsDiscipline d;
    const int64_t ppb = -12345;
    for (uint64_t s = 0; s < 3; s++) {
        const Edge e = edgeAt(s, ppb, 0);
        d.onEdge(e.mono, e.clock, true);
    }
    TEST_ASSERT_FALSE(d.hasRate());
    d.reset();
    for (uint64_t s = 600; s < 603; s++) {
        const Edge e = edgeAt(s, ppb, 0);
        d.onEdge(e.mono, e.clock, true);
    }
    TEST_ASSERT_TRUE(d.hasRate());
    TEST_ASSERT_INT32_WITHIN(10, ppb, d.ratePpb());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_no_fix_is_never_used);
    RUN_TEST(test_lone_edge_is_not_used);
    RUN_TEST(test_nearest_second_is_chosen);
    RUN_TEST(test_clock_too_far_is_not_used);
    RUN_TEST(test_glitch_is_rejected);
    RUN_TEST(test_missed_edge_is_tolerated);
    RUN_TEST(test_spacing_beyond_tolerance_is_rejected);
    RUN_TEST(test_rate_is_measured_with_the_right_sign);
    RUN_TEST(test_no_rate_from_a_short_baseline);
    RUN_TEST(test_rate_spans_bursts);
    exit(UNITY_END());
}

void loop() {}
