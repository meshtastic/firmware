// Unit tests for LnaAutoSelector::addSample() in src/mesh/LnaAutoSelector.cpp - the decision behind
// Config.LoRaConfig.fem_lna_mode = AUTO on boards with a switchable FEM LNA, whatever its gain.
//
// The firmware pairs each noise-floor reading with one taken on the other RX path and feeds the
// pair here. RSSI is not gain-compensated, so the rise when the LNA is switched in is compared to
// the LNA's gain: a rise equal to the gain means outside noise sets the floor and the LNA buys
// nothing, a rise short of it is the sensitivity the LNA buys. The selector switches only on a
// full window, with a hysteresis band, and treats an implausibly large benefit as compression.
//
// Regressions guarded: a decision taken before a window is full, or one that flips on every window
// inside the hysteresis band, toggles the LNA on noise. Readings leaking from one window into the
// next delay a switch the new window justifies. Averaging instead of taking the median lets a
// packet that lands in one or two pairs decide the window. Losing the compression guard turns a strong
// signal that compresses the LNA into a reason to keep it switched in, and for an LNA whose gain is
// at or below the cap, turns a switch that does nothing into a reason to keep it in.
#include "Arduino.h"
#include "LnaAutoSelector.h"
#include "TestUtil.h"
#include <unity.h>

// Any fixed gain well above the cap will do: the arithmetic below is relative to it.
static constexpr int16_t kGainDb = 20;
// A bypassed floor typical of a 250 kHz channel.
static constexpr int16_t kFloorOff = -110;

// Feeds one window in which the LNA buys benefitDb (rise = gain - benefit).
static void feedWindow(LnaAutoSelector &sel, int16_t benefitDb)
{
    for (uint8_t i = 0; i < LnaAutoSelector::WINDOW_SAMPLES; i++)
        sel.addSample(kFloorOff + kGainDb - benefitDb, kFloorOff);
}

void setUp(void) {}
void tearDown(void) {}

// --- the two ends the feature exists for ---

void test_noise_limited_site_bypasses_the_lna(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    feedWindow(sel, 0);
    TEST_ASSERT_FALSE(sel.isLnaOn());
    TEST_ASSERT_EQUAL_INT32(0, sel.getLastBenefitTenthsDb());
}

void test_quiet_site_switches_the_lna_in(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(false);
    feedWindow(sel, 5);
    TEST_ASSERT_TRUE(sel.isLnaOn());
    TEST_ASSERT_EQUAL_INT32(50, sel.getLastBenefitTenthsDb());
}

// --- only a full window decides ---

// addSample() reports completion of a window, not a change: the caller logs every completed window.

void test_no_decision_before_the_window_is_full(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    for (uint8_t i = 0; i + 1 < LnaAutoSelector::WINDOW_SAMPLES; i++) {
        TEST_ASSERT_FALSE(sel.addSample(kFloorOff + kGainDb, kFloorOff));
        TEST_ASSERT_TRUE(sel.isLnaOn());
    }
    TEST_ASSERT_TRUE(sel.addSample(kFloorOff + kGainDb, kFloorOff));
    TEST_ASSERT_FALSE(sel.isLnaOn());
}

// Window 1 bypasses (benefit 0). Window 2 buys 3 dB on its own, which enables; averaged with
// window 1 it would be 1.5 dB, inside the band, and the LNA would stay bypassed.
void test_windows_do_not_carry_readings_forward(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    feedWindow(sel, 0);
    feedWindow(sel, 3);
    TEST_ASSERT_TRUE(sel.isLnaOn());
}

// Seen on a Heltec V4.3: packets arriving during two pairs' readings gave rises of 2 and 17 dB in a
// window of steady 24-25 dB rises. Averaged, the window came out 1.7 dB, inside the band, and the
// LNA stayed in; the median ignores them.
void test_packets_during_a_few_pairs_do_not_sway_the_window(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    for (uint8_t i = 0; i < LnaAutoSelector::WINDOW_SAMPLES - 2; i++)
        sel.addSample(kFloorOff + kGainDb, kFloorOff);
    sel.addSample(-69, -71);
    sel.addSample(kFloorOff + 17, kFloorOff);
    TEST_ASSERT_FALSE(sel.isLnaOn());
    TEST_ASSERT_EQUAL_INT32(0, sel.getLastBenefitTenthsDb());
}

// --- hysteresis ---

// Half the pairs buy 1 dB and half 2 dB: a 1.5 dB median, inside the band, from integer readings.
static void feedBandWindow(LnaAutoSelector &sel)
{
    for (uint8_t i = 0; i < LnaAutoSelector::WINDOW_SAMPLES; i++)
        sel.addSample(kFloorOff + kGainDb - (i % 2 ? 1 : 2), kFloorOff);
}

void test_inside_the_band_an_enabled_lna_stays_enabled(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    feedBandWindow(sel);
    TEST_ASSERT_TRUE(sel.isLnaOn());
    TEST_ASSERT_EQUAL_INT32(15, sel.getLastBenefitTenthsDb());
}

void test_inside_the_band_a_bypassed_lna_stays_bypassed(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(false);
    feedBandWindow(sel);
    TEST_ASSERT_FALSE(sel.isLnaOn());
}

// The band edges are inclusive on the switching side.
void test_band_edges_switch(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(false);
    feedWindow(sel, LnaAutoSelector::ENABLE_BENEFIT_DB);
    TEST_ASSERT_TRUE(sel.isLnaOn());
    feedWindow(sel, LnaAutoSelector::DISABLE_BENEFIT_DB);
    TEST_ASSERT_FALSE(sel.isLnaOn());
}

// --- compression guard ---

// A rise far short of the gain is not a quiet site: the LNA's noise-figure improvement cannot
// explain it, so a strong signal has compressed the LNA, and that is a reason to bypass it.
void test_implausible_benefit_bypasses_the_lna(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    feedWindow(sel, LnaAutoSelector::MAX_PLAUSIBLE_BENEFIT_DB + 1);
    TEST_ASSERT_FALSE(sel.isLnaOn());
}

void test_benefit_at_the_plausible_limit_keeps_the_lna(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(false);
    feedWindow(sel, LnaAutoSelector::MAX_PLAUSIBLE_BENEFIT_DB);
    TEST_ASSERT_TRUE(sel.isLnaOn());
}

// An LNA switch that changes nothing (a hardware override holding the LNA out) reads as a benefit
// equal to the gain. For a gain at or below the cap that must still bypass, not read as a quiet site.
void test_switch_with_no_effect_bypasses_a_low_gain_lna(void)
{
    for (int16_t gain = LnaAutoSelector::ENABLE_BENEFIT_DB + 1; gain <= LnaAutoSelector::MAX_PLAUSIBLE_BENEFIT_DB + 1; gain++) {
        LnaAutoSelector sel(gain);
        sel.reset(true);
        for (uint8_t i = 0; i < LnaAutoSelector::WINDOW_SAMPLES; i++)
            sel.addSample(kFloorOff, kFloorOff);
        TEST_ASSERT_FALSE_MESSAGE(sel.isLnaOn(), "no-effect switch kept the LNA in");
    }
}

// --- reset ---

// reset() discards a partial window: a mode change starts the measurement over.
void test_reset_discards_a_partial_window(void)
{
    LnaAutoSelector sel(kGainDb);
    sel.reset(true);
    for (uint8_t i = 0; i + 1 < LnaAutoSelector::WINDOW_SAMPLES; i++)
        sel.addSample(kFloorOff + kGainDb, kFloorOff);
    sel.reset(true);
    TEST_ASSERT_FALSE(sel.addSample(kFloorOff + kGainDb, kFloorOff));
    TEST_ASSERT_TRUE(sel.isLnaOn());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_noise_limited_site_bypasses_the_lna);
    RUN_TEST(test_quiet_site_switches_the_lna_in);
    RUN_TEST(test_no_decision_before_the_window_is_full);
    RUN_TEST(test_windows_do_not_carry_readings_forward);
    RUN_TEST(test_packets_during_a_few_pairs_do_not_sway_the_window);
    RUN_TEST(test_inside_the_band_an_enabled_lna_stays_enabled);
    RUN_TEST(test_inside_the_band_a_bypassed_lna_stays_bypassed);
    RUN_TEST(test_band_edges_switch);
    RUN_TEST(test_implausible_benefit_bypasses_the_lna);
    RUN_TEST(test_benefit_at_the_plausible_limit_keeps_the_lna);
    RUN_TEST(test_switch_with_no_effect_bypasses_a_low_gain_lna);
    RUN_TEST(test_reset_discards_a_partial_window);
    exit(UNITY_END());
}

void loop() {}
