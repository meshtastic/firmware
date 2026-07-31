// Unit tests for shouldArmFixHold() / fixHoldInForce() in src/gps/GPS.cpp - the post-lock
// ephemeris hold.
//
// In power-saving mode (gps_update_interval above GPS_UPDATE_ALWAYS_ON_THRESHOLD_MS) the GPS holds
// for up to 20s after a lock to download ephemeris, then publishes and sleeps. The predicate below
// decides, once per GPS thread cycle that has a location, whether a hold should be armed.
//
// The case that matters is a hold that was consumed by a publish which did not sleep: GPS::runOnce()
// clears fixHoldEnds whenever it publishes, but only calls down() when the search timed out or a
// hold expired. If the predicate treats "not holding" as a reason to skip, nothing re-arms, nothing
// publishes, and the receiver stays powered until searchedTooLong() fires.
#include "Arduino.h"
#include "TestUtil.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include <cstdint>
#include <unity.h>

// The predicates live beside their only caller in src/gps/GPS.cpp rather than in a header of their
// own; the native test build compiles that file, so declaring the prototypes here is enough. A
// signature change breaks the link rather than silently diverging from the definition.
bool fixHoldInForce(uint32_t fixHoldEnds, uint32_t threadIntervalMs);
bool holdJustExpired(uint32_t fixHoldEnds);
bool shouldArmFixHold(bool hasValidLocation, uint8_t prevFixQual, uint32_t fixHoldEnds, uint32_t threadIntervalMs);

// GPS_THREAD_INTERVAL, spelled out so the suite does not pull in GPS.h and its hardware deps.
static constexpr uint32_t kThreadInterval = 200;

// The two hold durations the firmware uses: GPS_FIX_HOLD_MAX_MS, and a short one.
static constexpr uint32_t kHoldMs = 20 * 1000;

void setUp(void)
{
    Time::setTestMillis(0);
}
void tearDown(void)
{
    Time::useRealClock();
}

// Arms a hold at the current test time and returns the resulting fixHoldEnds.
static uint32_t armHoldNow(uint32_t holdMs = kHoldMs)
{
    return Time::getMillis() + holdMs;
}

// --- the reasons to arm ---

// First lock of a cycle: hasValidLocation is still false on the rising edge.
void test_arms_on_the_first_lock_of_a_cycle(void)
{
    Time::setTestMillis(50 * 1000);
    TEST_ASSERT_TRUE(shouldArmFixHold(false, 3, 0, kThreadInterval));
}

// Lock after the receiver was off: down() zeroes fixQual, so prev_fixQual is 0 on the way back up.
void test_arms_on_the_first_lock_after_the_gps_was_off(void)
{
    Time::setTestMillis(50 * 1000);
    TEST_ASSERT_TRUE(shouldArmFixHold(true, 0, 0, kThreadInterval));
}

// The regression. A publish that did not sleep leaves hasValidLocation set, prev_fixQual non-zero
// and fixHoldEnds cleared to 0. Nothing else in runOnce() re-arms, so if this returns false the
// GPS never holds, never publishes again and never calls down() until the search times out.
void test_arms_after_a_publish_cleared_the_hold_without_sleeping(void)
{
    Time::setTestMillis(50 * 1000);
    TEST_ASSERT_TRUE_MESSAGE(shouldArmFixHold(true, 3, 0, kThreadInterval),
                             "fixHoldEnds == 0 means 'not holding', which is a reason to arm");
}

void test_arms_once_the_hold_has_expired(void)
{
    Time::setTestMillis(50 * 1000);
    const uint32_t fixHoldEnds = armHoldNow();

    Time::advanceTestMillis(kHoldMs + kThreadInterval);
    TEST_ASSERT_TRUE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));
}

// --- the reason not to arm ---

void test_does_not_arm_while_a_hold_is_in_force(void)
{
    Time::setTestMillis(50 * 1000);
    const uint32_t fixHoldEnds = armHoldNow();

    Time::advanceTestMillis(kHoldMs / 2);
    TEST_ASSERT_FALSE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));
}

// The GPS_THREAD_INTERVAL grace period: at the exact deadline the hold has not yet expired, because
// the next cycle is one interval away.
void test_does_not_arm_in_the_thread_interval_grace_after_the_deadline(void)
{
    Time::setTestMillis(50 * 1000);
    const uint32_t fixHoldEnds = armHoldNow();

    Time::advanceTestMillis(kHoldMs); // exactly at the deadline
    TEST_ASSERT_FALSE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));

    Time::advanceTestMillis(kThreadInterval - 1);
    TEST_ASSERT_FALSE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));

    Time::advanceTestMillis(1); // deadline + GPS_THREAD_INTERVAL, inclusive boundary
    TEST_ASSERT_TRUE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));
}

// --- across the 32-bit wrap ---

// A hold armed just before the wrap must still be held through it. The naive form this replaced
// (`(fixHoldEnds + GPS_THREAD_INTERVAL) < millis()`) read as expired for the whole pre-wrap window,
// re-arming the hold on every single cycle.
void test_does_not_arm_while_a_hold_straddling_the_wrap_is_in_force(void)
{
    Time::setTestMillis(0xFFFFFF00u); // 256ms short of the wrap
    const uint32_t fixHoldEnds = armHoldNow();

    TEST_ASSERT_FALSE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));

    Time::advanceTestMillis(0x200u); // now past the wrap, still inside the hold
    TEST_ASSERT_FALSE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));

    Time::advanceTestMillis(kHoldMs); // well past the deadline, still past the wrap
    TEST_ASSERT_TRUE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));
}

// The deadline itself wrapping (fixHoldEnds numerically below millis()) must not read as expired.
void test_holds_when_the_deadline_wraps_but_now_has_not(void)
{
    Time::setTestMillis(0xFFFFFF00u);
    const uint32_t fixHoldEnds = armHoldNow(); // wraps to ~0x4CFF

    TEST_ASSERT_TRUE_MESSAGE(fixHoldEnds < Time::getMillis(), "test setup: the deadline must have wrapped");
    TEST_ASSERT_FALSE(shouldArmFixHold(true, 3, fixHoldEnds, kThreadInterval));
}

// --- the two readings of the same sentinel ---

// runOnce() asks two questions of fixHoldEnds and they take opposite answers when nothing is armed:
// "should I arm one?" (yes) and "did one just expire, so publish and sleep?" (no). Both are derived
// from fixHoldInForce(), which is the only place the sentinel is interpreted.
void test_no_hold_means_arm_but_does_not_mean_expired(void)
{
    Time::setTestMillis(50 * 1000);

    TEST_ASSERT_FALSE_MESSAGE(fixHoldInForce(0, kThreadInterval), "a hold that was never armed is not in force");
    TEST_ASSERT_TRUE_MESSAGE(shouldArmFixHold(true, 3, 0, kThreadInterval), "...so it is a reason to arm one");
    TEST_ASSERT_FALSE_MESSAGE(holdJustExpired(0), "...but not a reason to publish and sleep");
}

// holdJustExpired()'s sentinel guard is load-bearing on every cycle, not just past the half-range:
// fixHoldInForce() calls an unarmed hold "not in force", so negating it alone reads as expired.
void test_only_an_armed_hold_can_expire(void)
{
    Time::setTestMillis(50 * 1000);
    const uint32_t fixHoldEnds = armHoldNow();

    TEST_ASSERT_FALSE_MESSAGE(holdJustExpired(fixHoldEnds), "still inside the hold");

    Time::advanceTestMillis(kHoldMs); // the deadline itself, no grace interval at this site
    TEST_ASSERT_TRUE_MESSAGE(holdJustExpired(fixHoldEnds), "the deadline is the moment to publish and sleep");

    TEST_ASSERT_TRUE_MESSAGE(!fixHoldInForce(0, 0), "test premise: the negation alone calls an unarmed hold expired");
    TEST_ASSERT_FALSE_MESSAGE(holdJustExpired(0), "so the sentinel test is what keeps it from expiring");
}

// The `fixHoldEnds != 0` term inside fixHoldInForce() looks redundant, and for the first half of
// each wrap cycle it is: deadlinePassed(0 + interval) is true once uptime exceeds one interval, so
// "not in force" would fall out of the arithmetic on its own. Past 2^31 ms of uptime it flips.
// deadlinePassed() is an unsigned half-range test, so `now - interval` lands in the top half and
// the sentinel reads as a deadline ~24.9 days in the FUTURE - an unarmed hold would look like one
// in force for the whole second half of every cycle, and nothing would ever re-arm.
void test_the_sentinel_guard_is_load_bearing_past_the_half_range(void)
{
    Time::setTestMillis(0x90000000u); // ~27.8 days of uptime, past the 24.85-day half-range point

    // The arithmetic alone now says "not yet" for the sentinel...
    TEST_ASSERT_FALSE_MESSAGE(Throttle::deadlinePassed(0 + kThreadInterval),
                              "test premise: past half-range the sentinel reads as a future deadline");

    // ...so the explicit sentinel test is the only thing keeping the answer right.
    TEST_ASSERT_FALSE_MESSAGE(fixHoldInForce(0, kThreadInterval), "an unarmed hold is never in force");
    TEST_ASSERT_TRUE_MESSAGE(shouldArmFixHold(true, 3, 0, kThreadInterval), "...so a hold must still be armed");
}

void test_hold_in_force_tracks_the_deadline(void)
{
    Time::setTestMillis(50 * 1000);
    const uint32_t fixHoldEnds = armHoldNow();

    TEST_ASSERT_TRUE(fixHoldInForce(fixHoldEnds, kThreadInterval));

    Time::advanceTestMillis(kHoldMs + kThreadInterval);
    TEST_ASSERT_FALSE(fixHoldInForce(fixHoldEnds, kThreadInterval));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_no_hold_means_arm_but_does_not_mean_expired);
    RUN_TEST(test_only_an_armed_hold_can_expire);
    RUN_TEST(test_the_sentinel_guard_is_load_bearing_past_the_half_range);
    RUN_TEST(test_hold_in_force_tracks_the_deadline);
    RUN_TEST(test_arms_on_the_first_lock_of_a_cycle);
    RUN_TEST(test_arms_on_the_first_lock_after_the_gps_was_off);
    RUN_TEST(test_arms_after_a_publish_cleared_the_hold_without_sleeping);
    RUN_TEST(test_arms_once_the_hold_has_expired);
    RUN_TEST(test_does_not_arm_while_a_hold_is_in_force);
    RUN_TEST(test_does_not_arm_in_the_thread_interval_grace_after_the_deadline);
    RUN_TEST(test_does_not_arm_while_a_hold_straddling_the_wrap_is_in_force);
    RUN_TEST(test_holds_when_the_deadline_wraps_but_now_has_not);
    exit(UNITY_END());
}

void loop() {}
