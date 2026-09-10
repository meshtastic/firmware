// Unit tests for TrackballInterruptBase::updatePress(): short/long classification, the interrupt
// latch that keeps a tap released between polls, and repeat timing across the millis() rollover.
#include "TestUtil.h"
#include "UptimeClock.h"
#include "input/TrackballInterruptBase.h"
#include <unity.h>

namespace
{
// updatePress() is protected: expose it without touching any GPIO.
class PressProbe : public TrackballInterruptBase
{
  public:
    PressProbe() : TrackballInterruptBase("tbpress") {}
    using TrackballInterruptBase::PressResult;
    using TrackballInterruptBase::updatePress;
};

constexpr uint32_t LONG_PRESS_MS = 500;
constexpr uint32_t LONG_REPEAT_MS = 300;
constexpr uint32_t START_MS = 100000;
} // namespace

void setUp(void)
{
    Time::setTestMillis(START_MS);
}

void tearDown(void)
{
    Time::useRealClock();
}

void test_tap_released_before_poll_emits_short()
{
    PressProbe tb;
    TEST_ASSERT_TRUE(PressProbe::PressResult::Short == tb.updatePress(true, START_MS, false));
}

void test_held_then_released_under_threshold_emits_short()
{
    PressProbe tb;
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(true, START_MS, true));

    Time::advanceTestMillis(LONG_PRESS_MS - 1);
    TEST_ASSERT_TRUE(PressProbe::PressResult::Short == tb.updatePress(false, 0, false));
}

void test_still_held_under_threshold_emits_nothing()
{
    PressProbe tb;
    tb.updatePress(true, START_MS, true);

    Time::advanceTestMillis(LONG_PRESS_MS / 2);
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(false, 0, true));
}

void test_hold_emits_long_repeats_at_the_repeat_interval()
{
    PressProbe tb;
    tb.updatePress(true, START_MS, true);

    Time::advanceTestMillis(LONG_PRESS_MS);
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));

    Time::advanceTestMillis(LONG_REPEAT_MS - 1);
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(false, 0, true));

    Time::advanceTestMillis(1);
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));
}

void test_release_after_long_press_emits_nothing()
{
    PressProbe tb;
    tb.updatePress(true, START_MS, true);

    Time::advanceTestMillis(LONG_PRESS_MS);
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));

    Time::advanceTestMillis(10);
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(false, 0, false));
}

void test_press_after_release_is_tracked_again()
{
    PressProbe tb;
    tb.updatePress(true, START_MS, true);
    Time::advanceTestMillis(10);
    TEST_ASSERT_TRUE(PressProbe::PressResult::Short == tb.updatePress(false, 0, false));

    Time::advanceTestMillis(10);
    const uint32_t secondIrq = Time::getMillis();
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(true, secondIrq, true));

    Time::advanceTestMillis(LONG_PRESS_MS);
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));
}

void test_idle_poll_emits_nothing()
{
    PressProbe tb;
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(false, 0, false));
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(false, 0, true));
}

void test_hold_across_the_millis_wrap()
{
    PressProbe tb;
    const uint32_t nearWrap = 0xFFFFFF00u;
    Time::setTestMillis(nearWrap);
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(true, nearWrap, true));

    // Crosses the 32-bit rollover mid-press; a raw millis() compare would fire or stall here.
    Time::advanceTestMillis(LONG_PRESS_MS);
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));
}

// A delayed first poll must not report a long hold as a tap just because the pin is already high.
void test_long_hold_released_before_first_poll_is_not_short()
{
    PressProbe tb;
    const uint32_t irq = Time::getMillis();
    Time::advanceTestMillis(LONG_PRESS_MS);
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(true, irq, false));
}

// A repeat emitted exactly when the clock reads 0 must not be mistaken for "no repeat sent yet".
void test_repeat_emitted_at_clock_zero_still_waits()
{
    PressProbe tb;
    const uint32_t beforeWrap = 0u - LONG_PRESS_MS; // start + LONG_PRESS_MS wraps to exactly 0
    Time::setTestMillis(beforeWrap);
    tb.updatePress(true, beforeWrap, true);

    Time::advanceTestMillis(LONG_PRESS_MS);
    TEST_ASSERT_EQUAL_UINT32(0, Time::getMillis());
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));

    Time::advanceTestMillis(LONG_REPEAT_MS - 1);
    TEST_ASSERT_TRUE(PressProbe::PressResult::None == tb.updatePress(false, 0, true));

    Time::advanceTestMillis(1);
    TEST_ASSERT_TRUE(PressProbe::PressResult::LongRepeat == tb.updatePress(false, 0, true));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_tap_released_before_poll_emits_short);
    RUN_TEST(test_held_then_released_under_threshold_emits_short);
    RUN_TEST(test_still_held_under_threshold_emits_nothing);
    RUN_TEST(test_hold_emits_long_repeats_at_the_repeat_interval);
    RUN_TEST(test_release_after_long_press_emits_nothing);
    RUN_TEST(test_press_after_release_is_tracked_again);
    RUN_TEST(test_idle_poll_emits_nothing);
    RUN_TEST(test_hold_across_the_millis_wrap);
    RUN_TEST(test_long_hold_released_before_first_poll_is_not_short);
    RUN_TEST(test_repeat_emitted_at_clock_zero_still_waits);
    exit(UNITY_END());
}

void loop() {}
