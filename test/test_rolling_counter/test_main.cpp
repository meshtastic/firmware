// Unit tests for RollingCounter. The case that matters is the span sum() covers: an
// under-sized ring reports WindowMs - BucketMs, and counting the edge bucket whole reports more.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "modules/Telemetry/Sensor/RollingCounter.h"
#include <unity.h>

static constexpr uint32_t kMinute = 60UL * 1000;
static constexpr uint32_t kWindow = 60 * kMinute;
static constexpr uint32_t kBucket = 5 * kMinute;

using Counter = RollingCounter<kWindow, kBucket>;

void setUp()
{
    Time::setTestMillis(1000);
}

void tearDown()
{
    Time::useRealClock();
}

// Everything added inside the window is still counted at the far edge.
void test_counts_within_window()
{
    Counter c;
    for (int i = 0; i < 10; i++) {
        c.add();
        Time::advanceTestMillis(kMinute);
    }
    TEST_ASSERT_EQUAL_UINT32(10, c.sum());
}

// Expiry is exact to one bucket, not to the event: nothing records where inside a bucket an event
// fell, so it is wholly counted to WindowMs, wholly gone by WindowMs + BucketMs, decaying between.
void test_expires_within_one_bucket_of_the_hour()
{
    Counter c;
    c.add(100);

    Time::advanceTestMillis(kWindow - kMinute);
    TEST_ASSERT_EQUAL_UINT32(100, c.sum()); // 59 minutes old, wholly inside

    uint32_t previous = 100;
    for (int i = 0; i < 7; i++) { // walk a full bucket past the hour
        Time::advanceTestMillis(kMinute);
        uint32_t current = c.sum();
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(previous, current); // decays, never grows back
        previous = current;
    }
    TEST_ASSERT_EQUAL_UINT32(0, previous);
}

// The span must not shrink to 55 minutes as the current bucket fills. One event per
// minute for well over an hour means a correct 60-minute window always holds 60.
void test_span_stays_sixty_minutes()
{
    Counter c;
    for (int i = 0; i < 60; i++) {
        c.add();
        Time::advanceTestMillis(kMinute);
    }
    // Steady state: sample at every minute across two more bucket widths. A ring that
    // under-covers dips to 55, one that over-covers climbs to 65.
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL_UINT32(60, c.sum());
        c.add();
        Time::advanceTestMillis(kMinute);
    }
}

// Buckets must not be recycled while any part of them is still inside the window.
void test_bucket_not_dropped_early()
{
    Counter c;
    c.add(7); // lands in the first bucket

    // Step to just under an hour in bucket-sized hops; the batch stays counted throughout.
    for (uint32_t elapsed = 0; elapsed + kBucket < kWindow; elapsed += kBucket) {
        Time::advanceTestMillis(kBucket);
        TEST_ASSERT_EQUAL_UINT32(7, c.sum());
    }
}

// Going quiet for longer than the ring leaves nothing behind, and the counter still works.
void test_long_idle_gap()
{
    Counter c;
    c.add(3);
    Time::advanceTestMillis(5 * kWindow);
    TEST_ASSERT_EQUAL_UINT32(0, c.sum());

    c.add(2);
    TEST_ASSERT_EQUAL_UINT32(2, c.sum());
}

// A burst far larger than the bucket count still costs the same fixed memory, and is carried
// whole while it is inside the window.
void test_burst_survives_whole()
{
    Counter c;
    c.add(50000);
    Time::advanceTestMillis(kWindow - kMinute);
    TEST_ASSERT_EQUAL_UINT32(50000, c.sum());
}

// Weighting the edge bucket must not overflow: 50000 * 240000 exceeds 32 bits, and a 32-bit
// product wraps to 11367 instead of 40000. Four of the bucket's five minutes are still inside.
void test_large_burst_at_window_edge()
{
    Counter c;
    c.add(50000);
    Time::advanceTestMillis(kWindow + kMinute);
    TEST_ASSERT_EQUAL_UINT32(40000, c.sum());
}

void test_reset_clears()
{
    Counter c;
    c.add(5);
    c.reset();
    TEST_ASSERT_EQUAL_UINT32(0, c.sum());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_counts_within_window);
    RUN_TEST(test_expires_within_one_bucket_of_the_hour);
    RUN_TEST(test_span_stays_sixty_minutes);
    RUN_TEST(test_bucket_not_dropped_early);
    RUN_TEST(test_long_idle_gap);
    RUN_TEST(test_burst_survives_whole);
    RUN_TEST(test_large_burst_at_window_edge);
    RUN_TEST(test_reset_clears);
    exit(UNITY_END());
}

void loop() {}
