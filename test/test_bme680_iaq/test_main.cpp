#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "modules/Telemetry/Sensor/BME680IaqEstimator.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// The estimator is pure math with no platform dependencies, so this suite has
// no feature guard: it runs everywhere the native tests run.

namespace
{
constexpr float CLEAN_GAS = 400000.0f; // ~clean-air gas resistance in Ohms
constexpr float REF_RH = 40.0f;

// Total update() calls before the first IAQ value can appear: the warm-up
// discards plus the burn-in history requirement
constexpr uint32_t CALLS_TO_READY = BME680IaqEstimator::WARMUP_DISCARD + BME680IaqEstimator::BURN_IN_SAMPLES;

/// Feed constant clean air until the estimator reports; returns the first IAQ
uint16_t makeReady(BME680IaqEstimator &est, float gasOhms = CLEAN_GAS, float rh = REF_RH)
{
    uint16_t iaq = 0xFFFF;
    for (uint32_t i = 0; i < CALLS_TO_READY; i++) {
        bool got = est.update(gasOhms, rh, &iaq);
        TEST_ASSERT_EQUAL_MESSAGE(i == CALLS_TO_READY - 1, got, "IAQ must appear exactly when burn-in completes");
    }
    return iaq;
}

/// On-disk hash contract (xor of the five words preceding xorHash), replicated
/// so corruption tests can forge otherwise-consistent state
uint32_t stateHash(const BME680IaqState &s)
{
    uint32_t words[5];
    memcpy(words, &s, sizeof(words));
    return words[0] ^ words[1] ^ words[2] ^ words[3] ^ words[4];
}
} // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Input validation ---

void test_rejects_invalid_gas()
{
    BME680IaqEstimator est;
    uint16_t iaq;
    TEST_ASSERT_FALSE(est.update(0.0f, REF_RH, &iaq));
    TEST_ASSERT_FALSE(est.update(-5000.0f, REF_RH, &iaq));
    TEST_ASSERT_FALSE(est.update(NAN, REF_RH, &iaq));
    TEST_ASSERT_FALSE(est.update(INFINITY, REF_RH, &iaq));
    // Invalid samples must not consume warm-up or burn-in progress
    makeReady(est);
}

void test_invalid_humidity_is_neutral()
{
    BME680IaqEstimator est;
    makeReady(est);
    uint16_t iaq = 0xFFFF;
    TEST_ASSERT_TRUE(est.update(CLEAN_GAS, NAN, &iaq));
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
    // The fallback must not have moved the ceiling: a subsequent valid sample
    // at the reference RH must still score 0 (catches a wrong fallback value,
    // which would poison the baseline upward via ALPHA_UP)
    TEST_ASSERT_TRUE(est.update(CLEAN_GAS, REF_RH, &iaq));
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
}

// --- Warm-up / burn-in gating ---

void test_no_output_until_burn_in()
{
    BME680IaqEstimator est;
    uint16_t iaq = 0xFFFF;
    for (uint32_t i = 0; i < CALLS_TO_READY - 1; i++)
        TEST_ASSERT_FALSE(est.update(CLEAN_GAS, REF_RH, &iaq));
    TEST_ASSERT_FALSE(est.ready());
    TEST_ASSERT_TRUE(est.update(CLEAN_GAS, REF_RH, &iaq));
    TEST_ASSERT_TRUE(est.ready());
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
}

// --- Scoring ---

void test_clean_air_scores_zero()
{
    BME680IaqEstimator est;
    TEST_ASSERT_EQUAL_UINT16(0, makeReady(est));
}

void test_band_mapping_from_baseline_ratio()
{
    // Gas dropping to 1/N of the clean baseline should land in the UI band
    // the design targets: 1.31x ~Good, 1.7x ~Moderate/Poor edge, 3x ~beep
    // threshold, 15x+ pegged at 500
    struct {
        float ratio;
        uint16_t expected;
        uint16_t tolerance;
    } cases[] = {
        {1.31f, 50, 6}, {1.7f, 98, 7}, {3.0f, 203, 8}, {15.0f, 499, 2}, {100.0f, 500, 1},
    };
    for (auto &c : cases) {
        BME680IaqEstimator est;
        makeReady(est);
        uint16_t iaq = 0;
        TEST_ASSERT_TRUE(est.update(CLEAN_GAS / c.ratio, REF_RH, &iaq));
        char msg[64];
        snprintf(msg, sizeof(msg), "ratio %.2f -> iaq %u", (double)c.ratio, iaq);
        TEST_ASSERT_UINT_WITHIN_MESSAGE(c.tolerance, c.expected, iaq, msg);
    }
}

void test_band_mapping_holds_for_high_resistance_sensors()
{
    // Fresh/very clean sensors legitimately read in the MOhm range; the
    // sanity clamp must not compress events there (regression: LN_CEIL_MAX
    // was once ln(~730k), blinding the estimator above that)
    BME680IaqEstimator est;
    TEST_ASSERT_EQUAL_UINT16(0, makeReady(est, 5000000.0f));
    uint16_t iaq = 0;
    TEST_ASSERT_TRUE(est.update(5000000.0f / 3.0f, REF_RH, &iaq));
    TEST_ASSERT_UINT_WITHIN(8, 203, iaq);
}

void test_floor_clamps_bound_extreme_pollution()
{
    // Baseline seeded from heavily polluted air is clamped up to LN_FLOOR...
    BME680IaqEstimator est;
    uint16_t iaq = 0xFFFF;
    for (uint32_t i = 0; i < CALLS_TO_READY; i++)
        est.update(1000.0f, REF_RH, &iaq);
    // ...so 1 kOhm scores as polluted relative to that floor, not as "normal"
    TEST_ASSERT_TRUE(est.update(1000.0f, REF_RH, &iaq));
    TEST_ASSERT_UINT_WITHIN(10, 297, iaq); // (ln(5000) - ln(1000)) / ln(15) * 500 = (8.517 - 6.908) / 2.708 * 500
    // gas at the floor itself reads clean
    TEST_ASSERT_TRUE(est.update(5000.0f, REF_RH, &iaq));
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
    // absurdly low readings rail at exactly 500 via the sample clamp
    TEST_ASSERT_TRUE(est.update(1.0f, REF_RH, &iaq));
    TEST_ASSERT_EQUAL_UINT16(500, iaq);
}

void test_humidity_comfort_penalty()
{
    // Present the same compensated log-resistance at 80 %RH: gas score stays
    // ~0, and only the outside-the-30-60-deadband humidity penalty remains
    BME680IaqEstimator est;
    makeReady(est);
    float gasAt80 = CLEAN_GAS * expf(-BME680IaqEstimator::KH * (80.0f - REF_RH));
    uint16_t iaq = 0xFFFF;
    TEST_ASSERT_TRUE(est.update(gasAt80, 80.0f, &iaq));
    TEST_ASSERT_UINT_WITHIN(8, 38, iaq); // 0.15 * (20/40 * 500) = 37.5

    // The dry side of the deadband penalizes symmetrically
    BME680IaqEstimator estDry;
    makeReady(estDry);
    float gasAt10 = CLEAN_GAS * expf(-BME680IaqEstimator::KH * (10.0f - REF_RH));
    TEST_ASSERT_TRUE(estDry.update(gasAt10, 10.0f, &iaq));
    TEST_ASSERT_UINT_WITHIN(8, 38, iaq);

    // Inside the deadband there is no penalty at all
    BME680IaqEstimator est2;
    makeReady(est2);
    float gasAt55 = CLEAN_GAS * expf(-BME680IaqEstimator::KH * (55.0f - REF_RH));
    TEST_ASSERT_TRUE(est2.update(gasAt55, 55.0f, &iaq));
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
}

// --- Baseline dynamics ---

void test_baseline_resists_sustained_pollution()
{
    BME680IaqEstimator est;
    makeReady(est);
    uint16_t iaq = 0;
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(est.update(100000.0f, REF_RH, &iaq));
        TEST_ASSERT_GREATER_THAN_UINT(200, iaq); // ln(4) -> ~256, must stay "bad"
    }
    // Back to clean air: the ceiling barely decayed, so the score snaps to 0
    TEST_ASSERT_TRUE(est.update(CLEAN_GAS, REF_RH, &iaq));
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
}

void test_baseline_rises_fast_toward_cleaner_air()
{
    BME680IaqEstimator est;
    makeReady(est, 300000.0f);
    uint16_t iaq = 0xFFFF;
    // Cleaner air scores 0 immediately and re-baselines within ~20 samples
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_TRUE(est.update(CLEAN_GAS, REF_RH, &iaq));
        TEST_ASSERT_EQUAL_UINT16(0, iaq);
    }
    // The old air now reads as polluted relative to the new baseline
    TEST_ASSERT_TRUE(est.update(300000.0f, REF_RH, &iaq));
    TEST_ASSERT_UINT_WITHIN(8, 53, iaq); // ln(400/300)/ln(15) * 500
}

// --- Persistence ---

void test_serialize_restore_roundtrip()
{
    BME680IaqEstimator est;
    makeReady(est);
    BME680IaqState state;
    est.serialize(&state, 1000000);
    TEST_ASSERT_EQUAL_UINT32(BME680IaqEstimator::MAGIC, state.magic);
    TEST_ASSERT_EQUAL_UINT32(stateHash(state), state.xorHash);
    TEST_ASSERT_EQUAL_UINT8(0, state.warmupRemaining);

    // Warm-up progress travels with the state: a restored estimator reports
    // on its very first sample (essential for one-sample-per-wake nodes)
    BME680IaqEstimator restored;
    TEST_ASSERT_TRUE(restored.restore(state, 1000000 + 3600));
    uint16_t iaq = 0;
    TEST_ASSERT_TRUE(restored.update(CLEAN_GAS / 3.0f, REF_RH, &iaq));
    TEST_ASSERT_UINT_WITHIN(8, 203, iaq);
}

void test_restore_mid_burn_in_continues_progress()
{
    BME680IaqEstimator est;
    uint16_t iaq;
    for (uint32_t i = 0; i < BME680IaqEstimator::WARMUP_DISCARD + 5; i++)
        est.update(CLEAN_GAS, REF_RH, &iaq);
    BME680IaqState state;
    est.serialize(&state, 0);

    BME680IaqEstimator restored;
    TEST_ASSERT_TRUE(restored.restore(state, 0));
    int producedAt = -1;
    for (int i = 1; i <= 40; i++) {
        if (restored.update(CLEAN_GAS, REF_RH, &iaq)) {
            producedAt = i;
            break;
        }
    }
    // 5 of 30 burn-in samples were banked before the "reboot"
    TEST_ASSERT_EQUAL_INT(BME680IaqEstimator::BURN_IN_SAMPLES - 5, producedAt);
}

void test_deep_sleep_node_converges_across_reboots()
{
    // Simulate a power-saving SENSOR role: one sample per wake, RAM wiped
    // between wakes, state restored+persisted each cycle. Must produce IAQ
    // after exactly warm-up + burn-in wakes, not never.
    BME680IaqState state;
    bool haveState = false;
    uint16_t iaq = 0xFFFF;
    int producedAt = -1;
    for (int wake = 1; wake <= 50; wake++) {
        BME680IaqEstimator est;
        if (haveState)
            TEST_ASSERT_TRUE_MESSAGE(est.restore(state, 0), "persisted progress must restore on every wake");
        if (est.update(CLEAN_GAS, REF_RH, &iaq)) {
            producedAt = wake;
            break;
        }
        est.serialize(&state, 0);
        haveState = true;
    }
    TEST_ASSERT_EQUAL_INT((int)CALLS_TO_READY, producedAt);
    TEST_ASSERT_EQUAL_UINT16(0, iaq);
}

void test_restore_rejects_corruption()
{
    BME680IaqEstimator est;
    makeReady(est);
    BME680IaqState good;
    est.serialize(&good, 1000000);
    BME680IaqEstimator target;

    BME680IaqState bad = good;
    bad.magic ^= 1;
    TEST_ASSERT_FALSE(target.restore(bad, 1000000));

    bad = good;
    bad.version = BME680IaqEstimator::VERSION + 1;
    bad.xorHash = stateHash(bad);
    TEST_ASSERT_FALSE(target.restore(bad, 1000000));

    bad = good;
    bad.xorHash ^= 0xDEADBEEF;
    TEST_ASSERT_FALSE(target.restore(bad, 1000000));

    // Consistent hash but implausible ceiling (the ceiling check only applies
    // once samples have been accepted)
    bad = good;
    bad.lnCeiling = 20.0f;
    bad.xorHash = stateHash(bad);
    TEST_ASSERT_FALSE(target.restore(bad, 1000000));

    bad = good;
    bad.lnCeiling = NAN;
    bad.xorHash = stateHash(bad);
    TEST_ASSERT_FALSE(target.restore(bad, 1000000));
}

void test_restore_staleness()
{
    BME680IaqEstimator est;
    makeReady(est);
    BME680IaqState state;
    est.serialize(&state, 1000000);

    BME680IaqEstimator target;
    TEST_ASSERT_FALSE(target.restore(state, 1000000 + BME680IaqEstimator::STATE_MAX_AGE_SECS + 1));
    TEST_ASSERT_TRUE(target.restore(state, 1000000 + BME680IaqEstimator::STATE_MAX_AGE_SECS - 1));

    // Unknown age (no RTC at save time or now) is accepted rather than discarded
    est.serialize(&state, 0);
    BME680IaqEstimator target2;
    TEST_ASSERT_TRUE(target2.restore(state, 2000000));
    est.serialize(&state, 1000000);
    BME680IaqEstimator target3;
    TEST_ASSERT_TRUE(target3.restore(state, 0));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== BME680 IAQ estimator ===\n");
    RUN_TEST(test_rejects_invalid_gas);
    RUN_TEST(test_invalid_humidity_is_neutral);
    RUN_TEST(test_no_output_until_burn_in);
    RUN_TEST(test_clean_air_scores_zero);
    RUN_TEST(test_band_mapping_from_baseline_ratio);
    RUN_TEST(test_band_mapping_holds_for_high_resistance_sensors);
    RUN_TEST(test_floor_clamps_bound_extreme_pollution);
    RUN_TEST(test_humidity_comfort_penalty);
    RUN_TEST(test_baseline_resists_sustained_pollution);
    RUN_TEST(test_baseline_rises_fast_toward_cleaner_air);
    RUN_TEST(test_serialize_restore_roundtrip);
    RUN_TEST(test_restore_mid_burn_in_continues_progress);
    RUN_TEST(test_deep_sleep_node_converges_across_reboots);
    RUN_TEST(test_restore_staleness);
    RUN_TEST(test_restore_rejects_corruption);

    exit(UNITY_END());
}

void loop() {}
