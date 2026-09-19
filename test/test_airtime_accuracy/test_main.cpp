// Accuracy of the airtime windows, measured against an exact oracle.
//
// The oracle is a raw log of [start, end] airtime spans. True occupancy over any window is a direct
// integration over those spans, so it shares no code - and therefore no defects - with the bucket
// rings it judges. That is the one property a second implementation of the bucket walk could not
// have: it would only ever agree with itself.
//
// Eight figures, printed as one table. Each is asserted as a BOUND, never an exact float, because a
// number that moves with a compiler flag is not evidence. A stage that claims to fix a defect
// tightens only its own bounds and leaves the others pinned, so an unintended regression elsewhere
// fails this suite rather than being absorbed into a new number.
#include "Arduino.h"
#include "MeshRadio.h"
#include "NodeDB.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "airtime.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unity.h>
#include <vector>

// --- the bounds ---------------------------------------------------------------------------------
//
// STAGE 0 (this commit): every bound below is a CHARACTERISATION - it encodes today's measured
// wrong number, so a later stage's improvement shows up as an edit here and nowhere else. A stage
// tightens only the bounds for the defect it claims, leaving the rest pinned, so a regression
// elsewhere fails this suite rather than being absorbed into a new number.
//
// These are MEASURED on develop @ fa87e7d, not carried over from design4's simulation. Where the
// two differ the measurement wins: design4's chanutil figures (-8.3 / -16.7) are RELATIVE percent,
// which at a 40% reading is the -2.79 / -6.78 pp seen here. D3 and D8 reproduce it almost exactly.
// D7's absolute level is NOT comparable to design4 §6.9 - this scenario parks the offered load on
// the gate rather than ramping through it, so only the stage-to-stage movement means anything.
#define D1_SPREAD_BOUND 20      // min   - measured 18
#define D2_ERR_BOUND 28.0f      // min   - measured 5.95 spread, 26.60 concentrated
#define D3_PEAK_BOUND 120.0f    // %     - measured 118.03 against a true 100.0
#define D4_MEAN_BOUND 3.5f      // pp    - measured -2.79
#define D4_WORST_BOUND 8.0f     // pp    - measured -6.78
#define D5_SAWTOOTH_BOUND 10.0f // pp    - measured 8.48
#define D6_DISAGREE_BOUND 14.0f // %     - measured 11.8, of which 99% permissive
#define D7_BOUND 45.0f          // %     - measured 26.6-40.1 across the five spacings
#define D8_MEAN_BOUND 0.10f     // pp    - measured -0.041
#define D8_WORST_BOUND 0.10f    // pp    - measured -0.056
static meshtastic_Config_LoRaConfig_RegionCode savedRegion;
static meshtastic_Config_DeviceConfig_Role savedRole;
static bool savedOverrideDutyCycle;

void setUp(void)
{
    Time::resetMonotonicForTests();
    savedRegion = config.lora.region;
    savedRole = config.device.role;
    savedOverrideDutyCycle = config.lora.override_duty_cycle;
}
void tearDown(void)
{
    Time::useRealClock();
    config.lora.region = savedRegion;
    config.device.role = savedRole;
    config.lora.override_duty_cycle = savedOverrideDutyCycle;
    initRegion();
}

// --- the oracle --------------------------------------------------------------------------------

struct Span {
    uint64_t start, end;
};
static std::vector<Span> g_spans;
static uint64_t g_nowMs = 0; // mirrors the injected clock, in the oracle's own arithmetic

static const uint32_t CHANUTIL_WINDOW_MS = 60u * 1000u;
static const uint32_t TXUTIL_WINDOW_MS = 3600u * 1000u;

static void clockReset(uint64_t atMs)
{
    Time::resetMonotonicForTests();
    Time::setTestMillis((uint32_t)atMs);
    Time::serviceMonotonic();
    g_nowMs = atMs;
    g_spans.clear();
}

static void clockAdvance(uint32_t ms)
{
    Time::advanceTestMillis(ms);
    Time::serviceMonotonic();
    g_nowMs += ms;
}

// Log to AirTime exactly as the radio does - at completion - and record what really happened.
static void emit(AirTime &a, uint32_t airtimeMs)
{
    a.logAirtime(TX_LOG, airtimeMs);
    g_spans.push_back({g_nowMs - airtimeMs, g_nowMs});
}

// Exact occupancy of the trailing `windowMs`, as a percentage of the nominal window - the same
// denominator AirTime uses, so the two are comparable. Spans are appended in time order, so walking
// back and stopping at the first that ended before the window is O(window), not O(history).
static float trueOccupancyPercent(uint32_t windowMs)
{
    const uint64_t winEnd = g_nowMs;
    const uint64_t winStart = g_nowMs >= windowMs ? g_nowMs - windowMs : 0;
    uint64_t busy = 0;
    for (size_t i = g_spans.size(); i-- > 0;) {
        const Span &s = g_spans[i];
        if (s.end <= winStart)
            break;
        const uint64_t lo = std::max(s.start, winStart);
        const uint64_t hi = std::min(s.end, winEnd);
        if (hi > lo)
            busy += hi - lo;
    }
    return (float)busy / (float)windowMs * 100.0f;
}

// The truth getSilentMinutes() is approximating: with no further traffic, the least whole minutes
// until a true sliding-hour occupancy sits at or under the limit.
static uint8_t oracleSilentMinutes(float dutyCycle)
{
    const uint64_t saveNow = g_nowMs;
    for (uint8_t m = 0; m <= 60; m++) {
        g_nowMs = saveNow + (uint64_t)m * 60u * 1000u;
        const float pct = trueOccupancyPercent(TXUTIL_WINDOW_MS);
        if (pct <= dutyCycle) {
            g_nowMs = saveNow;
            return m;
        }
    }
    g_nowMs = saveNow;
    return 60;
}

// --- traffic -----------------------------------------------------------------------------------

// Advances to whichever event is next - a packet or a sample - so neither grid quantises the other.
template <typename F>
static void runTraffic(AirTime &a, uint32_t durationMs, uint32_t packetMs, uint32_t spacingMs, uint32_t sampleMs, F onSample)
{
    const uint64_t endAt = g_nowMs + durationMs;
    uint64_t nextEmit = g_nowMs + spacingMs;
    uint64_t nextSample = g_nowMs + sampleMs;

    while (g_nowMs < endAt) {
        const uint64_t next = std::min(std::min(nextEmit, nextSample), endAt);
        clockAdvance((uint32_t)(next - g_nowMs));
        if (g_nowMs == nextEmit) {
            emit(a, packetMs);
            nextEmit += spacingMs;
        }
        if (g_nowMs == nextSample) {
            onSample(a);
            nextSample += sampleMs;
        }
    }
}

// The same, but the airtime arrives in bursts of `burstPackets` back to back every `spacingMs`.
// Burst placement against the bucket edge is what the 6 x 10 s geometry aliases on.
template <typename F>
static void runBursts(AirTime &a, uint32_t durationMs, uint32_t packetMs, uint8_t burstPackets, uint32_t spacingMs,
                      uint32_t sampleMs, F onSample)
{
    const uint64_t endAt = g_nowMs + durationMs;
    uint64_t nextBurst = g_nowMs + spacingMs;
    uint64_t nextSample = g_nowMs + sampleMs;

    while (g_nowMs < endAt) {
        const uint64_t next = std::min(std::min(nextBurst, nextSample), endAt);
        clockAdvance((uint32_t)(next - g_nowMs));
        if (g_nowMs == nextBurst) {
            for (uint8_t i = 0; i < burstPackets; i++) {
                clockAdvance(packetMs);
                emit(a, packetMs);
            }
            nextBurst += spacingMs;
        }
        if (g_nowMs >= nextSample) {
            onSample(a);
            nextSample = g_nowMs + sampleMs;
        }
    }
}

// --- the figures -------------------------------------------------------------------------------

static const uint32_t LONG_FAST_MS = 2034;  // SF11/BW250, 237 B
static const uint32_t LONG_SLOW_MS = 14164; // SF12/BW125/CR8 at max size - 1.4x a 10 s bucket

struct Figures {
    uint8_t d1_spread = 0;
    float d2_meanAbsErr = 0, d2_meanAbsErrConc = 0;
    float d3_saturatedPeak = 0;
    float d4_biasMean = 0, d4_biasWorst = 0;
    float d5_sawtooth = 0;
    float d6_disagree = 0, d6_permissiveShare = 0;
    float d7[5] = {0};
    float d8_biasMean = 0, d8_biasWorst = 0;
};
static Figures F;
static char g_msg[192];

// The one line that moves between stages, and the only edit this commit makes to the demo suite.
// It used to mirror Router::send() - read the percentage, hand it back - because that coupling was
// itself one of the defects under measure. getSilentMinutes() now reads the ring itself, so there
// is nothing to hand it. Every figure below is untouched.
static uint8_t silentMinutes(AirTime &a, float dutyCycle)
{
    return a.getSilentMinutes(dutyCycle);
}

// Identical traffic laid down at every minute phase of the ring.
// `concentrated` packs the same 210 packets into 7 minutes instead of spreading them over 42.
// design4 called the old walk "indistinguishable from guessing when airtime is concentrated, which
// is exactly when it is called" - a duty-cycle abort follows a burst, not a trickle - so the demo
// has to measure both or it flatters the defect.
static uint8_t silentMinutesAtPhase(uint32_t phaseSecs, float dutyCycle, uint8_t *oracleOut, bool concentrated = false)
{
    clockReset((uint64_t)phaseSecs * 1000u);
    AirTime a;
    const uint8_t minutes = concentrated ? 7 : 42;
    const uint8_t perMinute = concentrated ? 30 : 5;
    for (uint8_t m = 0; m < minutes; m++) {
        for (uint8_t k = 0; k < perMinute; k++)
            emit(a, LONG_FAST_MS);
        if (m + 1 < minutes)
            clockAdvance(60u * 1000u);
    }
    if (oracleOut)
        *oracleOut = oracleSilentMinutes(dutyCycle);
    return silentMinutes(a, dutyCycle);
}

void test_D1_silent_minutes_spread_over_ring_phase()
{
    uint8_t lo = 255, hi = 0;
    for (uint32_t p = 0; p < 60; p++) {
        const uint8_t m = silentMinutesAtPhase(p * 60u + 17u, 2.5f, nullptr);
        lo = std::min(lo, m);
        hi = std::max(hi, m);
    }
    F.d1_spread = (uint8_t)(hi - lo);
    snprintf(g_msg, sizeof(g_msg), "D1 spread %u min (lo %u, hi %u) - identical traffic must give one answer", F.d1_spread, lo,
             hi);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8_MESSAGE(D1_SPREAD_BOUND, F.d1_spread, g_msg);
}

void test_D2_silent_minutes_error_against_the_oracle()
{
    float sums[2] = {0, 0};
    uint8_t n = 0;
    for (uint32_t p = 0; p < 60; p += 3) {
        uint8_t oracle = 0;
        const uint8_t spread = silentMinutesAtPhase(p * 60u + 17u, 2.5f, &oracle);
        sums[0] += fabsf((float)spread - (float)oracle);
        const uint8_t conc = silentMinutesAtPhase(p * 60u + 17u, 2.5f, &oracle, true);
        sums[1] += fabsf((float)conc - (float)oracle);
        n++;
    }
    F.d2_meanAbsErr = sums[0] / (float)n;
    F.d2_meanAbsErrConc = sums[1] / (float)n;
    snprintf(g_msg, sizeof(g_msg), "D2 mean absolute error %.2f min spread / %.2f min concentrated, over %u phases",
             F.d2_meanAbsErr, F.d2_meanAbsErrConc, n);
    TEST_ASSERT_TRUE_MESSAGE(F.d2_meanAbsErr <= D2_ERR_BOUND && F.d2_meanAbsErrConc <= D2_ERR_BOUND, g_msg);
}

// A saturated channel is 100.0% occupied. A bucket that can hold more than its own period lets the
// window report more than its own length.
void test_D3_saturated_long_slow_channel()
{
    clockReset(0);
    AirTime a;
    float peak = 0;
    runTraffic(a, 120u * 1000u, LONG_SLOW_MS, LONG_SLOW_MS, 1000, [](AirTime &) {}); // warm-up
    runTraffic(a, 240u * 1000u, LONG_SLOW_MS, LONG_SLOW_MS, 1000,
               [&peak](AirTime &at) { peak = std::max(peak, at.channelUtilizationPercent()); });
    F.d3_saturatedPeak = peak;
    snprintf(g_msg, sizeof(g_msg), "D3 saturated LONG_SLOW peak %.2f%% against a true 100.0%%", F.d3_saturatedPeak);
    TEST_ASSERT_TRUE_MESSAGE(F.d3_saturatedPeak <= D3_PEAK_BOUND, g_msg);
}

// Constant offered load. D5 is the swing of the ERROR, not of the reading: the reading also moves
// because packets arrive at discrete instants, and that part is real. The error's swing is the
// artefact - what the window geometry adds on top of the truth.
void test_D4_D5_steady_load_bias_and_sawtooth()
{
    clockReset(0);
    AirTime a;
    const uint32_t spacing = (uint32_t)(LONG_FAST_MS / 0.40f); // ~40% occupancy
    float sum = 0, worst = 0, lo = 1e9f, hi = -1e9f;
    uint32_t n = 0;

    runTraffic(a, 120u * 1000u, LONG_FAST_MS, spacing, 1000, [](AirTime &) {}); // warm-up past a full window
    runTraffic(a, 600u * 1000u, LONG_FAST_MS, spacing, 1000, [&](AirTime &at) {
        const float got = at.channelUtilizationPercent();
        const float truth = trueOccupancyPercent(CHANUTIL_WINDOW_MS);
        const float err = got - truth;
        sum += err;
        worst = fabsf(err) > fabsf(worst) ? err : worst;
        lo = std::min(lo, err);
        hi = std::max(hi, err);
        n++;
    });

    F.d4_biasMean = sum / (float)n;
    F.d4_biasWorst = worst;
    F.d5_sawtooth = hi - lo;
    snprintf(g_msg, sizeof(g_msg), "D4 bias mean %.2f pp worst %.2f pp; D5 swing %.2f pp over %u samples", F.d4_biasMean,
             F.d4_biasWorst, F.d5_sawtooth, n);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(F.d4_biasMean) <= D4_MEAN_BOUND && fabsf(F.d4_biasWorst) <= D4_WORST_BOUND &&
                                 F.d5_sawtooth <= D5_SAWTOOTH_BOUND,
                             g_msg);
}

// What the bias costs the 40% gate, on a load ramped through it. The direction is the point: a
// permissive disagreement transmits when it should not.
void test_D6_gate_disagreement_on_a_ramp()
{
    uint32_t disagree = 0, permissive = 0, n = 0;

    for (int pct = 30; pct <= 50; pct += 2) {
        clockReset(0);
        AirTime a;
        const uint32_t spacing = (uint32_t)(LONG_FAST_MS / ((float)pct / 100.0f));
        runTraffic(a, 120u * 1000u, LONG_FAST_MS, spacing, 1000, [](AirTime &) {});
        runTraffic(a, 180u * 1000u, LONG_FAST_MS, spacing, 1000, [&](AirTime &at) {
            const bool gotOver = at.channelUtilizationPercent() > 40.0f;
            const bool trueOver = trueOccupancyPercent(CHANUTIL_WINDOW_MS) > 40.0f;
            if (gotOver != trueOver) {
                disagree++;
                if (!gotOver)
                    permissive++;
            }
            n++;
        });
    }

    F.d6_disagree = 100.0f * (float)disagree / (float)n;
    F.d6_permissiveShare = disagree ? 100.0f * (float)permissive / (float)disagree : 0.0f;
    snprintf(g_msg, sizeof(g_msg), "D6 %.1f%% of %u readings disagree, %.0f%% of those permissive", F.d6_disagree, n,
             F.d6_permissiveShare);
    TEST_ASSERT_TRUE_MESSAGE(F.d6_disagree <= D6_DISAGREE_BOUND, g_msg);
}

// Burst spacing sweep. 10 s and 20 s are commensurate with the 10 s bucket and would flatter the
// result, so the sweep avoids them.
void test_D7_gate_disagreement_by_burst_spacing()
{
    static const uint32_t spacings[5] = {7, 11, 13, 17, 23};

    for (int i = 0; i < 5; i++) {
        const uint32_t spacingMs = spacings[i] * 1000u;
        // Hold the OFFERED LOAD at exactly 40% across every spacing, by sizing the packet to the
        // burst rather than rounding the burst to the packet. Rounding the count instead makes each
        // spacing a different load - at 7 s it lands at 29%, nowhere near the gate, and the figure
        // reads 0.0% for want of anything to disagree about.
        const uint8_t burst = (uint8_t)std::max(1.0f, roundf(0.40f * (float)spacingMs / (float)LONG_FAST_MS));
        const uint32_t packetMs = (uint32_t)(0.40f * (float)spacingMs / (float)burst);
        clockReset(0);
        AirTime a;
        uint32_t disagree = 0, n = 0;
        runBursts(a, 120u * 1000u, packetMs, burst, spacingMs, 1000, [](AirTime &) {});
        runBursts(a, 600u * 1000u, packetMs, burst, spacingMs, 1000, [&](AirTime &at) {
            const bool gotOver = at.channelUtilizationPercent() > 40.0f;
            const bool trueOver = trueOccupancyPercent(CHANUTIL_WINDOW_MS) > 40.0f;
            if (gotOver != trueOver)
                disagree++;
            n++;
        });
        F.d7[i] = 100.0f * (float)disagree / (float)n;
    }

    snprintf(g_msg, sizeof(g_msg), "D7 by spacing 7/11/13/17/23 s: %.1f %.1f %.1f %.1f %.1f %%", F.d7[0], F.d7[1], F.d7[2],
             F.d7[3], F.d7[4]);
    TEST_ASSERT_TRUE_MESSAGE(
        F.d7[0] <= D7_BOUND && F.d7[1] <= D7_BOUND && F.d7[2] <= D7_BOUND && F.d7[3] <= D7_BOUND && F.d7[4] <= D7_BOUND, g_msg);
}

// The same geometry on the 60 x 60 s window the duty cycle actually reads.
void test_D8_airutil_bias_over_the_hour()
{
    clockReset(0);
    AirTime a;
    const uint32_t spacing = (uint32_t)(LONG_FAST_MS / 0.025f); // ~2.5%, a duty-cycle-limited region
    float sum = 0, worst = 0;
    uint32_t n = 0;

    runTraffic(a, TXUTIL_WINDOW_MS, LONG_FAST_MS, spacing, 60u * 1000u, [](AirTime &) {}); // one full window
    runTraffic(a, TXUTIL_WINDOW_MS, LONG_FAST_MS, spacing, 60u * 1000u, [&](AirTime &at) {
        const float err = at.utilizationTXPercent() - trueOccupancyPercent(TXUTIL_WINDOW_MS);
        sum += err;
        worst = fabsf(err) > fabsf(worst) ? err : worst;
        n++;
    });

    F.d8_biasMean = sum / (float)n;
    F.d8_biasWorst = worst;
    snprintf(g_msg, sizeof(g_msg), "D8 airutil bias mean %.3f pp worst %.3f pp over %u samples", F.d8_biasMean, F.d8_biasWorst,
             n);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(F.d8_biasMean) <= D8_MEAN_BOUND && fabsf(F.d8_biasWorst) <= D8_WORST_BOUND, g_msg);
}

// Runs last. The table is the deliverable: paste it into the commit message beside the previous
// stage's, and the diff is the evidence that the stage did what it claimed.
void test_ZZ_print_the_demo_table()
{
    printf("\n");
    printf("+---- airtime accuracy demo suite -------------------------------+\n");
    printf("| D1 getSilentMinutes spread over 60 ring phases  %8u min  |\n", F.d1_spread);
    printf("| D2 gSM mean abs err  spread %6.2f min  concentrated %6.2f min |\n", F.d2_meanAbsErr, F.d2_meanAbsErrConc);
    printf("| D3 saturated LONG_SLOW chanutil (truth 100.0%%)  %8.2f %%    |\n", F.d3_saturatedPeak);
    printf("| D4 chanutil bias      mean %7.2f pp   worst %8.2f pp   |\n", F.d4_biasMean, F.d4_biasWorst);
    printf("| D5 chanutil error swing (geometry artefact)     %8.2f pp   |\n", F.d5_sawtooth);
    printf("| D6 gate disagreement on a ramp   %5.1f %% (%3.0f %% permissive) |\n", F.d6_disagree, F.d6_permissiveShare);
    printf("| D7 gate disagreement by burst spacing (s)                      |\n");
    printf("|      7 s %5.1f %%   11 s %5.1f %%   13 s %5.1f %%                 |\n", F.d7[0], F.d7[1], F.d7[2]);
    printf("|     17 s %5.1f %%   23 s %5.1f %%                                |\n", F.d7[3], F.d7[4]);
    printf("| D8 airutil bias       mean %7.3f pp   worst %8.3f pp   |\n", F.d8_biasMean, F.d8_biasWorst);
    printf("+----------------------------------------------------------------+\n\n");
    TEST_PASS();
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_D1_silent_minutes_spread_over_ring_phase);
    RUN_TEST(test_D2_silent_minutes_error_against_the_oracle);
    RUN_TEST(test_D3_saturated_long_slow_channel);
    RUN_TEST(test_D4_D5_steady_load_bias_and_sawtooth);
    RUN_TEST(test_D6_gate_disagreement_on_a_ramp);
    RUN_TEST(test_D7_gate_disagreement_by_burst_spacing);
    RUN_TEST(test_D8_airutil_bias_over_the_hour);
    RUN_TEST(test_ZZ_print_the_demo_table);
    exit(UNITY_END());
}

void loop() {}
