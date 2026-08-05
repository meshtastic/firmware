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

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(100, report[0]);
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

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
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

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
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

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
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

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_EQUAL_UINT32(777, report[1]);
}

// --- report routing: which array each type feeds ---
//
// Deliberately asserted through the public API rather than the public bucket arrays. Those arrays
// become private later; a test that reads them would have to be rewritten then, and a test that
// would have to be rewritten is not pinning a contract.

void test_tx_log_feeds_tx_report_and_tx_utilization()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(TX_LOG, 6000);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(6000, report[0]);
    // TX is the only type that reaches all three stores.
    TEST_ASSERT_TRUE(a.utilizationTXPercent() > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, a.channelUtilizationPercent());
}

// Duty cycle is about our own transmissions. Counting received airtime here would throttle a node
// for other people's traffic.
void test_rx_log_feeds_rx_report_but_not_tx_utilization()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(RX_LOG, 6000);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(RX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(6000, report[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.utilizationTXPercent());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, a.channelUtilizationPercent());
}

void test_rx_all_log_feeds_only_the_noise_report()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(RX_ALL_LOG, 6000);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(RX_ALL_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(6000, report[0]);

    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_TRUE(a.airtimeReport(RX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.utilizationTXPercent());
}

// The shared property: channel utilisation counts all airtime, ours and other people's.
void test_every_report_type_feeds_channel_utilization()
{
    const reportTypes types[] = {TX_LOG, RX_LOG, RX_ALL_LOG};
    for (uint8_t i = 0; i < 3; i++) {
        Time::resetMonotonicForTests();
        Time::setTestMillis(0);
        AirTime a;

        a.logAirtime(types[i], 6000);

        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 10.0f, a.channelUtilizationPercent(),
                                         "every report type must reach channelUtilization");
    }
}

void test_report_types_do_not_cross_contaminate()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(TX_LOG, 111);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(RX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_TRUE(a.airtimeReport(RX_ALL_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
}

// --- airtimeReport() contract ---

void test_airtimeReport_rejects_a_null_buffer()
{
    Time::setTestMillis(0);
    AirTime a;

    TEST_ASSERT_FALSE(a.airtimeReport(TX_LOG, nullptr, PERIODS_TO_LOG));
}

void test_airtimeReport_rejects_a_count_above_the_log_depth()
{
    Time::setTestMillis(0);
    AirTime a;

    uint32_t report[PERIODS_TO_LOG + 1] = {0};
    TEST_ASSERT_FALSE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG + 1));
}

void test_airtimeReport_accepts_a_partial_count()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 42);

    const uint32_t sentinel = 0xDEADBEEFu;
    uint32_t report[PERIODS_TO_LOG];
    for (uint8_t i = 0; i < PERIODS_TO_LOG; i++)
        report[i] = sentinel;

    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, 2));

    TEST_ASSERT_EQUAL_UINT32(42, report[0]);
    TEST_ASSERT_EQUAL_UINT32(0, report[1]);
    for (uint8_t i = 2; i < PERIODS_TO_LOG; i++)
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(sentinel, report[i], "a partial count must not write past it");
}

void test_airtimeReport_rejects_an_unknown_report_type()
{
    Time::setTestMillis(0);
    AirTime a;

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_FALSE(a.airtimeReport(static_cast<reportTypes>(99), report, PERIODS_TO_LOG));
}

// The regression guard for the copy-out: if anyone reintroduces the array-returning form, the
// caller's buffer starts tracking the live buckets and this fails.
void test_airtimeReport_returns_a_snapshot_not_an_alias()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 100);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(100, report[0]);

    a.logAirtime(TX_LOG, 900);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100, report[0], "the copy must not follow the live bucket");
}

// --- storage conventions ---
//
// The class holds two incompatible orderings and nothing states which is which. The report arrays
// are shift-ordered (slot 0 newest); channelUtilization and utilizationTX are modular rings indexed
// by uptime phase. Reading one as if it were the other is a defect that has already happened once.

void test_report_arrays_are_shift_ordered_slot_zero_newest()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(TX_LOG, 100); // oldest
    Time::advanceTestMillis(3600u * 1000u);
    a.logAirtime(TX_LOG, 200);
    Time::advanceTestMillis(3600u * 1000u);
    a.logAirtime(TX_LOG, 300); // newest

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(300, report[0], "slot 0 is the newest hour");
    TEST_ASSERT_EQUAL_UINT32(200, report[1]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100, report[2], "index is age in hours, not ring phase");
}

// Slot 0 covers only the time since the last rotation, so a consumer treating it as a whole hour
// under-reports. getSecondsSinceBoot() % getSecondsPerPeriod() is how much of it has elapsed - the
// contract the HTTP JSON publishes both halves of.
void test_report_slot_zero_is_a_partial_hour()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 100);

    Time::advanceTestMillis(3600u * 1000u); // rotate; slot 0 is now brand new
    Time::advanceTestMillis(120u * 1000u);  // and 120s into its hour
    a.logAirtime(TX_LOG, 250);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(250, report[0], "slot 0 holds only airtime since the boundary");
    TEST_ASSERT_EQUAL_UINT32(100, report[1]);

    const uint32_t elapsedInSlotZero = a.getSecondsSinceBoot() % a.getSecondsPerPeriod();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(120, elapsedInSlotZero, "the partial-hour phase must be recoverable");
}

// --- first sync and seeding ---

// The firstTime branch seeds secSinceBoot from the clock. A naive `secSinceBoot = 0` would make the
// first access believe 500s had elapsed and rotate 500s of empty windows through.
void test_first_sync_seeds_from_current_uptime_not_zero()
{
    Time::setTestMillis(500u * 1000u);
    AirTime a;

    TEST_ASSERT_EQUAL_UINT32(500, a.getSecondsSinceBoot());

    a.logAirtime(RX_LOG, 6000);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 10.0f, a.channelUtilizationPercent(),
                                     "no phantom decay from the pre-construction uptime");
}

void test_first_sync_zeroes_every_window()
{
    Time::setTestMillis(1234u * 1000u);
    AirTime a;

    uint32_t report[PERIODS_TO_LOG] = {0};
    const reportTypes types[] = {TX_LOG, RX_LOG, RX_ALL_LOG};
    for (uint8_t t = 0; t < 3; t++) {
        TEST_ASSERT_TRUE(a.airtimeReport(types[t], report, PERIODS_TO_LOG));
        for (uint8_t i = 0; i < PERIODS_TO_LOG; i++)
            TEST_ASSERT_EQUAL_UINT32(0, report[i]);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.channelUtilizationPercent());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.utilizationTXPercent());
}

void test_late_construction_does_not_backdate_airtime()
{
    Time::setTestMillis(7200u * 1000u); // two hours of uptime before AirTime exists
    AirTime a;

    a.logAirtime(TX_LOG, 400);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(400, report[0], "airtime belongs to the current bucket, not a backdated one");
    for (uint8_t i = 1; i < PERIODS_TO_LOG; i++)
        TEST_ASSERT_EQUAL_UINT32(0, report[i]);
}

// --- sync idempotency ---

void test_repeated_sync_within_one_second_does_not_rotate()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(RX_LOG, 6000);

    Time::advanceTestMillis(500); // sub-second: the nowSecs == secSinceBoot early return
    for (uint8_t i = 0; i < 5; i++) {
        (void)a.channelUtilizationPercent();
        (void)a.getSecondsSinceBoot();
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, a.channelUtilizationPercent());
}

// Every public entry point syncs. Calling several in the same interval must not compound the
// rotation. Guards the later restructure where each becomes a lock-and-delegate wrapper: two
// instances see identical wall time and identical airtime, and differ only in how many entry
// points were called.
void test_rotation_is_once_per_second_regardless_of_entry_point()
{
    Time::setTestMillis(0);
    AirTime oneEntryPoint;
    AirTime everyEntryPoint;

    oneEntryPoint.logAirtime(RX_LOG, 6000);
    everyEntryPoint.logAirtime(RX_LOG, 6000);

    Time::advanceTestMillis(20u * 1000u); // two 10s buckets crossed

    uint32_t scratch[PERIODS_TO_LOG] = {0};
    (void)everyEntryPoint.getSecondsSinceBoot();
    (void)everyEntryPoint.utilizationTXPercent();
    everyEntryPoint.airtimeRotatePeriod();
    (void)everyEntryPoint.airtimeReport(TX_LOG, scratch, PERIODS_TO_LOG);
    (void)everyEntryPoint.isTxAllowedChannelUtil();

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, oneEntryPoint.channelUtilizationPercent(),
                                     everyEntryPoint.channelUtilizationPercent(),
                                     "rotation must be driven by the clock, not by the call count");
}

void test_period_constants_are_stable()
{
    Time::setTestMillis(0);
    AirTime a;

    // Public API: ContentHandler sizes its buffer from getPeriodsToLog().
    TEST_ASSERT_EQUAL_UINT8(8, a.getPeriodsToLog());
    TEST_ASSERT_EQUAL_UINT32(3600, a.getSecondsPerPeriod());
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(PERIODS_TO_LOG, a.getPeriodsToLog(), "the accessor and the macro must agree");
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

    // report routing
    RUN_TEST(test_tx_log_feeds_tx_report_and_tx_utilization);
    RUN_TEST(test_rx_log_feeds_rx_report_but_not_tx_utilization);
    RUN_TEST(test_rx_all_log_feeds_only_the_noise_report);
    RUN_TEST(test_every_report_type_feeds_channel_utilization);
    RUN_TEST(test_report_types_do_not_cross_contaminate);
    // airtimeReport() contract
    RUN_TEST(test_airtimeReport_rejects_a_null_buffer);
    RUN_TEST(test_airtimeReport_rejects_a_count_above_the_log_depth);
    RUN_TEST(test_airtimeReport_accepts_a_partial_count);
    RUN_TEST(test_airtimeReport_rejects_an_unknown_report_type);
    RUN_TEST(test_airtimeReport_returns_a_snapshot_not_an_alias);
    // storage conventions
    RUN_TEST(test_report_arrays_are_shift_ordered_slot_zero_newest);
    RUN_TEST(test_report_slot_zero_is_a_partial_hour);
    // first sync and seeding
    RUN_TEST(test_first_sync_seeds_from_current_uptime_not_zero);
    RUN_TEST(test_first_sync_zeroes_every_window);
    RUN_TEST(test_late_construction_does_not_backdate_airtime);
    // sync idempotency
    RUN_TEST(test_repeated_sync_within_one_second_does_not_rotate);
    RUN_TEST(test_rotation_is_once_per_second_regardless_of_entry_point);
    RUN_TEST(test_period_constants_are_stable);
    exit(UNITY_END());
}

void loop() {}
