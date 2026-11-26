// Gesture timing in TouchScreenBase::runOnce() (src/input/TouchScreenBase.cpp) on a board whose touch interrupt
// timestamps each press and release (TOUCH_INT_LEVEL, read through the touchEdgeTimes() hook).
//
// The touch thread runs in the main loop, so a slow screen redraw can keep it waiting for hundreds of ms. A press is
// then measured from the interrupt's edge times instead of from when the thread noticed it. That only holds while the
// edges belong to the press being measured, and while the timed start is used consistently:
//   - A long hold first noticed late keeps its real start, so it is still a LONG_PRESS. With the start replaced by the
//     time of the late poll, a 700 ms hold reads as a 150 ms TAP: a missed long press, the bug the interrupt timing
//     exists to fix.
//   - A tap just under TIME_LONG_PRESS whose release falls inside the release grace window is one TAP. The grace
//     window still counts the finger as down, and measured from the earlier, timed start the held-press check fired a
//     LONG_PRESS before the release reported the TAP as well.
//   - Two taps inside one stall must not merge into a LONG_PRESS. The second tap overwrites the first's release, so
//     timing the first tap against the second's release reads as one long press: a capital letter instead of a tap.
//   - A tap whose release is noticed late is still a TAP: the reason TOUCH_INT_LEVEL exists.
// The test clock drives runOnce() through Time::getMillis(), and FakeTouch stands in for the controller and its
// interrupt, so each case states exactly when the finger moved and when the thread looked.
#include "TestUtil.h"
#include "UptimeClock.h"
#include "input/TouchScreenBase.h"
#include <unity.h>
#include <vector>

#ifndef TIME_LONG_PRESS
#define TIME_LONG_PRESS 400
#endif

namespace
{

class FakeTouch : public TouchScreenBase
{
  public:
    FakeTouch() : TouchScreenBase("fakeTouch", 320, 480) {}

    static constexpr char TAP = TOUCH_ACTION_TAP;
    static constexpr char LONG_PRESS = TOUCH_ACTION_LONG_PRESS;

    // The finger and the interrupt's view of it: a press stamps its down edge, a release its up edge.
    void press(uint32_t atMs)
    {
        fingerDown = true;
        downMs = atMs;
        edgesSeen = true;
    }
    void release(uint32_t atMs)
    {
        fingerDown = false;
        upMs = atMs;
    }

    // The touch thread running at this time.
    void pollAt(uint32_t atMs)
    {
        Time::setTestMillis(atMs);
        runOnce();
    }

    std::vector<char> events;

  protected:
    bool getTouch(int16_t &x, int16_t &y) override
    {
        x = 100;
        y = 100;
        return fingerDown;
    }
    void onEvent(const TouchEvent &event) override { events.push_back(event.touchEvent); }
    bool touchEdgeTimes(uint32_t &down, uint32_t &up) override
    {
        if (!edgesSeen)
            return false;
        down = downMs;
        up = upMs;
        return true;
    }

  private:
    bool fingerDown = false;
    bool edgesSeen = false;
    uint32_t downMs = 0;
    uint32_t upMs = 0;
};

FakeTouch *touch = nullptr;

void assertEvents(const std::vector<char> &expected)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.size(), touch->events.size(), "number of touch events");
    for (size_t i = 0; i < expected.size(); i++)
        TEST_ASSERT_EQUAL_INT8(expected[i], touch->events[i]);
}

} // namespace

void setUp(void)
{
    touch = new FakeTouch();
}

void tearDown(void)
{
    delete touch;
    touch = nullptr;
    Time::useRealClock();
}

// Down at 10000, first seen at 10600: the finger has been down 600 ms, so that poll reports the LONG_PRESS, and the
// release at 10700 adds nothing. Starting the press at the late poll instead reads the hold as a 150 ms TAP.
static void test_longHold_firstSeenLate_isLongPress(void)
{
    touch->press(10000);
    touch->pollAt(10600);
    touch->release(10700);
    touch->pollAt(10750);
    assertEvents({FakeTouch::LONG_PRESS});
}

// A 390 ms tap, last sampled down at 10380 and released at 10390. At 10405 the release grace still counts the finger
// as down, 405 ms after the timed start; the release edge says it is up, so only the release decides: one TAP.
static void test_tapJustUnderThreshold_inReleaseGrace_isOneTap(void)
{
    touch->press(10000);
    touch->pollAt(10100);
    touch->pollAt(10380);
    touch->release(10390);
    touch->pollAt(10405);
    touch->pollAt(10430);
    assertEvents({FakeTouch::TAP});
}

// Seen down at 10100; the thread then stalls through a 200 ms tap ending at 10200 and a second one from 10400 to
// 10600. The edges are the second tap's, so the first can't be timed: no event, rather than a 600 ms LONG_PRESS.
static void test_twoTapsInOneStall_areNotALongPress(void)
{
    touch->press(10000);
    touch->pollAt(10100);
    touch->release(10200);
    touch->press(10400);
    touch->release(10600);
    touch->pollAt(10700);
    for (char event : touch->events)
        TEST_ASSERT_TRUE_MESSAGE(event != FakeTouch::LONG_PRESS, "two short taps reported as a LONG_PRESS");
}

// A 150 ms tap whose release the thread only sees 650 ms later is still a TAP, timed by the interrupt.
static void test_tapReleaseSeenLate_isTap(void)
{
    touch->press(10000);
    touch->pollAt(10050);
    touch->release(10150);
    touch->pollAt(10800);
    assertEvents({FakeTouch::TAP});
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_longHold_firstSeenLate_isLongPress);
    RUN_TEST(test_tapJustUnderThreshold_inReleaseGrace_isOneTap);
    RUN_TEST(test_twoTapsInOneStall_areNotALongPress);
    RUN_TEST(test_tapReleaseSeenLate_isTap);
    exit(UNITY_END());
}

void loop() {}
