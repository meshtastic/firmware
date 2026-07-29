// Unit tests for src/mesh/Throttle.{h,cpp} - the firmware's primary elapsed-time helper.
//
// Throttle backs ~94 call sites, so its rollover behaviour is the single most load-bearing
// time property in the tree. These tests drive the injected clock across the 32-bit millis()
// wrap, which is not otherwise reachable in a test.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include <cstdint>
#include <unity.h>

void setUp(void) {}
void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into other suites
}

// --- basic window semantics ---

void test_isWithinTimespan_true_inside_window()
{
    Time::setTestMillis(10000);
    TEST_ASSERT_TRUE(Throttle::isWithinTimespanMs(9500, 1000)); // 500ms elapsed of a 1000ms window
}

void test_isWithinTimespan_false_outside_window()
{
    Time::setTestMillis(10000);
    TEST_ASSERT_FALSE(Throttle::isWithinTimespanMs(8000, 1000)); // 2000ms elapsed
}

// The boundary is exclusive: elapsed == interval is NOT "within".
void test_isWithinTimespan_boundary_is_exclusive()
{
    Time::setTestMillis(10000);
    TEST_ASSERT_FALSE(Throttle::isWithinTimespanMs(9000, 1000)); // exactly 1000ms elapsed
    TEST_ASSERT_TRUE(Throttle::isWithinTimespanMs(9001, 1000));  // 999ms elapsed
}

// --- hasElapsed is the exact complement ---

void test_hasElapsed_is_complement_of_isWithinTimespan()
{
    Time::setTestMillis(10000);
    const uint32_t cases[][2] = {{9500, 1000}, {8000, 1000}, {9000, 1000}, {10000, 1}, {0, 5000}};
    for (auto &c : cases) {
        TEST_ASSERT_EQUAL(!Throttle::isWithinTimespanMs(c[0], c[1]), Throttle::hasElapsed(c[0], c[1]));
    }
}

void test_hasElapsed_boundary_is_inclusive()
{
    Time::setTestMillis(10000);
    TEST_ASSERT_TRUE(Throttle::hasElapsed(9000, 1000));  // exactly 1000ms elapsed
    TEST_ASSERT_FALSE(Throttle::hasElapsed(9001, 1000)); // 999ms elapsed
}

// --- rollover: the headline property ---

// A window opened just before the 32-bit wrap must still close correctly after it.
void test_isWithinTimespan_survives_millis_wrap()
{
    const uint32_t lastRun = 0xFFFFFF00u; // 256ms before the wrap
    Time::setTestMillis(lastRun);

    Time::advanceTestMillis(100); // 0xFFFFFF64 - still before the wrap
    TEST_ASSERT_TRUE(Throttle::isWithinTimespanMs(lastRun, 1000));

    Time::advanceTestMillis(200); // wraps to 0x0000002C - 300ms elapsed in total
    TEST_ASSERT_TRUE(Throttle::isWithinTimespanMs(lastRun, 1000));
    TEST_ASSERT_FALSE(Throttle::hasElapsed(lastRun, 1000));

    Time::advanceTestMillis(800); // 1100ms elapsed in total, well past the wrap
    TEST_ASSERT_FALSE(Throttle::isWithinTimespanMs(lastRun, 1000));
    TEST_ASSERT_TRUE(Throttle::hasElapsed(lastRun, 1000));
}

// The long-interval end of the range: a 24h window (the longest in the tree) across the wrap.
void test_long_interval_survives_wrap()
{
    const uint32_t dayMs = 24u * 60u * 60u * 1000u; // 86,400,000
    const uint32_t lastRun = 0xFFFFFF00u;
    Time::setTestMillis(lastRun);

    Time::advanceTestMillis(dayMs - 1);
    TEST_ASSERT_TRUE(Throttle::isWithinTimespanMs(lastRun, dayMs));

    Time::advanceTestMillis(1); // exactly one day elapsed
    TEST_ASSERT_TRUE(Throttle::hasElapsed(lastRun, dayMs));
}

// --- execute() ---

static int executeCount = 0;
static int deferCount = 0;
static void countExecute()
{
    executeCount++;
}
static void countDefer()
{
    deferCount++;
}

void test_execute_runs_first_time_then_throttles()
{
    executeCount = 0;
    deferCount = 0;
    Time::setTestMillis(5000);

    uint32_t last = 0; // 0 means "never run" to execute()
    TEST_ASSERT_TRUE(Throttle::execute(&last, 1000, countExecute, countDefer));
    TEST_ASSERT_EQUAL(1, executeCount);

    // Immediately again: deferred.
    TEST_ASSERT_FALSE(Throttle::execute(&last, 1000, countExecute, countDefer));
    TEST_ASSERT_EQUAL(1, executeCount);
    TEST_ASSERT_EQUAL(1, deferCount);

    // After the interval: runs again.
    Time::advanceTestMillis(1000);
    TEST_ASSERT_TRUE(Throttle::execute(&last, 1000, countExecute, countDefer));
    TEST_ASSERT_EQUAL(2, executeCount);
}

void test_execute_survives_millis_wrap()
{
    executeCount = 0;
    Time::setTestMillis(0xFFFFFF00u);

    uint32_t last = 0;
    TEST_ASSERT_TRUE(Throttle::execute(&last, 1000, countExecute)); // arms at 0xFFFFFF00
    TEST_ASSERT_EQUAL(1, executeCount);

    Time::advanceTestMillis(500);                                    // wraps past 0
    TEST_ASSERT_FALSE(Throttle::execute(&last, 1000, countExecute)); // not due yet
    TEST_ASSERT_EQUAL(1, executeCount);

    Time::advanceTestMillis(600); // 1100ms total
    TEST_ASSERT_TRUE(Throttle::execute(&last, 1000, countExecute));
    TEST_ASSERT_EQUAL(2, executeCount);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_isWithinTimespan_true_inside_window);
    RUN_TEST(test_isWithinTimespan_false_outside_window);
    RUN_TEST(test_isWithinTimespan_boundary_is_exclusive);
    RUN_TEST(test_hasElapsed_is_complement_of_isWithinTimespan);
    RUN_TEST(test_hasElapsed_boundary_is_inclusive);
    RUN_TEST(test_isWithinTimespan_survives_millis_wrap);
    RUN_TEST(test_long_interval_survives_wrap);
    RUN_TEST(test_execute_runs_first_time_then_throttles);
    RUN_TEST(test_execute_survives_millis_wrap);
    exit(UNITY_END());
}

void loop() {}
