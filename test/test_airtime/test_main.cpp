// Unit tests for src/airtime.{h,cpp} - AirTime::syncNow() and its rolling windows.
//
// syncNow() replaced a per-second runOnce() tick with monotonic-uptime bucket rotation so windows
// stay correct across light sleep. It now takes its seconds from Time::getUptimeSecs(), which is a
// pure read of a carry the main loop publishes via Time::serviceMonotonic(); these tests exercise
// the rotation/decay math on top of that, including across the 32-bit millis() wrap. The wrap cases
// therefore step the clock the way the main loop does - advance, then publish.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "airtime.h"
#include <cstdint>
#include <unity.h>

void setUp(void)
{
    // Absolute uptime assertions (e.g. getSecondsSinceBoot()) must not inherit wraps counted by
    // an earlier case that moved the test clock backwards via setTestMillis().
    Time::resetMonotonicForTests();
}
void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into other suites
}

// --- first sync / immediate writes ---

void test_logAirtime_writes_into_current_bucket_immediately()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(TX_LOG, 100);

    TEST_ASSERT_EQUAL_UINT32(100, a.airtimeReport(TX_LOG)[0]);
}

void test_getSecondsSinceBoot_tracks_elapsed_time()
{
    Time::setTestMillis(0);
    AirTime a;

    TEST_ASSERT_EQUAL_UINT32(0, a.getSecondsSinceBoot());
    Time::advanceTestMillis(5000);
    TEST_ASSERT_EQUAL_UINT32(5, a.getSecondsSinceBoot());
}

// --- hourly period rotation ---

void test_period_rotates_after_one_hour()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 500);

    Time::advanceTestMillis(3600u * 1000u); // exactly one SECONDS_PER_PERIOD

    uint32_t *report = a.airtimeReport(TX_LOG);
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);   // new period starts empty
    TEST_ASSERT_EQUAL_UINT32(500, report[1]); // old period shifted back one slot
}

// The property runOnce() alone could never exercise: several hours pass in a single sync (e.g. the
// device was light-sleeping), so the rotation has to walk forward more than one period at once.
void test_period_rotates_once_per_hour_crossed_while_asleep()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 200);

    Time::advanceTestMillis(3u * 3600u * 1000u); // 3 hours in one jump

    uint32_t *report = a.airtimeReport(TX_LOG);
    TEST_ASSERT_EQUAL_UINT32(200, report[3]);
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_EQUAL_UINT32(0, report[1]);
    TEST_ASSERT_EQUAL_UINT32(0, report[2]);
}

// More periods elapse than there are slots to rotate through: the whole history is stale, not just
// the oldest slot, so it must be wiped rather than rotated PERIODS_TO_LOG times.
void test_period_history_clears_when_asleep_longer_than_the_whole_log()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 999);

    Time::advanceTestMillis(9u * 3600u * 1000u); // 9 hours > PERIODS_TO_LOG (8)

    uint32_t *report = a.airtimeReport(TX_LOG);
    for (uint8_t i = 0; i < a.getPeriodsToLog(); i++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, report[i], "stale history must be cleared, not rotated in");
    }
}

// --- channel utilization: rolling 60s window ---

void test_channel_utilization_reflects_recent_airtime()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(RX_LOG, 6000); // 6s of airtime inside the 60s window

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, a.channelUtilizationPercent());
}

void test_channel_utilization_decays_once_the_60s_window_passes()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(RX_LOG, 6000);

    Time::advanceTestMillis(70u * 1000u); // longer than the 60s rolling window

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, a.channelUtilizationPercent());
}

void test_isTxAllowedChannelUtil_blocks_once_over_threshold()
{
    Time::setTestMillis(0);
    AirTime a;

    TEST_ASSERT_TRUE(a.isTxAllowedChannelUtil()); // nothing logged yet

    a.logAirtime(RX_LOG, 25000); // 25s / 60s = 41.7%, over the 40% default max
    TEST_ASSERT_FALSE(a.isTxAllowedChannelUtil());
}

// --- TX utilization: rolling 60-minute window ---

void test_tx_utilization_decays_once_the_60_minute_window_passes()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 60000); // 1 minute of TX airtime

    TEST_ASSERT_TRUE(a.utilizationTXPercent() > 0.0f);

    Time::advanceTestMillis(61u * 60u * 1000u); // longer than the 60-minute rolling window

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, a.utilizationTXPercent());
}

// --- the headline property: syncNow() must survive the 32-bit millis() wrap ---

void test_syncNow_survives_millis_wrap()
{
    const uint32_t beforeWrap = 4294967000u; // 296ms before the wrap, on a whole-second boundary
    Time::setTestMillis(beforeWrap);
    Time::serviceMonotonic(); // the main loop's publish, which is what carries the wrap
    AirTime a;

    TEST_ASSERT_EQUAL_UINT32(4294967u, a.getSecondsSinceBoot());

    Time::advanceTestMillis(1000); // crosses the wrap
    Time::serviceMonotonic();
    TEST_ASSERT_EQUAL_UINT32(4294968u, a.getSecondsSinceBoot());
}

// A bucket logged just before the wrap must still be the one that rotates out after it - pinning
// the same property test_period_rotates_after_one_hour checks, but across the wrap boundary.
void test_period_rotation_survives_millis_wrap()
{
    const uint32_t beforeWrap = 0xFFFFFFFFu - (3600u * 1000u) + 1; // one hour minus 1ms before the wrap
    Time::setTestMillis(beforeWrap);
    Time::serviceMonotonic();
    AirTime a;
    a.logAirtime(TX_LOG, 777);

    Time::advanceTestMillis(3600u * 1000u); // wraps partway through
    Time::serviceMonotonic();

    uint32_t *report = a.airtimeReport(TX_LOG);
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_EQUAL_UINT32(777, report[1]);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_logAirtime_writes_into_current_bucket_immediately);
    RUN_TEST(test_getSecondsSinceBoot_tracks_elapsed_time);
    RUN_TEST(test_period_rotates_after_one_hour);
    RUN_TEST(test_period_rotates_once_per_hour_crossed_while_asleep);
    RUN_TEST(test_period_history_clears_when_asleep_longer_than_the_whole_log);
    RUN_TEST(test_channel_utilization_reflects_recent_airtime);
    RUN_TEST(test_channel_utilization_decays_once_the_60s_window_passes);
    RUN_TEST(test_isTxAllowedChannelUtil_blocks_once_over_threshold);
    RUN_TEST(test_tx_utilization_decays_once_the_60_minute_window_passes);
    RUN_TEST(test_syncNow_survives_millis_wrap);
    RUN_TEST(test_period_rotation_survives_millis_wrap);
    exit(UNITY_END());
}

void loop() {}
