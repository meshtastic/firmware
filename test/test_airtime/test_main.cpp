// Unit tests for src/airtime.{h,cpp} - AirTime::syncNow() and its rolling windows.
//
// syncNow() replaced a per-second runOnce() tick with monotonic-uptime bucket rotation so windows
// stay correct across light sleep. It now takes its seconds from Time::getUptimeSecs(), which is a
// pure read of a carry the main loop publishes via Time::serviceMonotonic(); these tests exercise
// the rotation/decay math on top of that, including across the 32-bit millis() wrap. The wrap cases
// therefore step the clock the way the main loop does - advance, then publish.
#include "Arduino.h"
#include "MeshRadio.h"
#include "NodeDB.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "airtime.h"
#include <cstdint>
#include <cstdio>
#include <unity.h>

static meshtastic_Config_LoRaConfig_RegionCode savedRegion;
static meshtastic_Config_DeviceConfig_Role savedRole;
static bool savedOverrideDutyCycle;

void setUp(void)
{
    // Absolute uptime assertions (e.g. getSecondsSinceBoot()) must not inherit wraps counted by
    // an earlier case that moved the test clock backwards via setTestMillis().
    Time::resetMonotonicForTests();
    savedRegion = config.lora.region;
    savedRole = config.device.role;
    savedOverrideDutyCycle = config.lora.override_duty_cycle;
}
void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into other suites
    // Restore the duty-cycle globals here, not at the end of a test body: an assertion aborts the
    // body via longjmp and would leak the region into every later case. initRegion() on the way
    // out, because getEffectiveDutyCycle() dereferences myRegion.
    config.lora.region = savedRegion;
    config.device.role = savedRole;
    config.lora.override_duty_cycle = savedOverrideDutyCycle;
    initRegion();
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
// Asserted through the public API, not the bucket arrays: those are private.

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
// Two orderings: the report arrays are shift-ordered (slot 0 newest); channelUtilization and
// utilizationTX are modular rings indexed by uptime phase. Reading one as the other is a defect.

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

// Slot 0 covers only the time since the last rotation; treating it as a whole hour under-reports.
// getSecondsSinceBoot() % getSecondsPerPeriod() recovers the elapsed part.
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

// The firstTime branch seeds secSinceBoot from the clock; seeding 0 would rotate 500s of empty
// windows through on first access.
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
// rotation: two instances see identical wall time and airtime, differing only in how many entry
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

// ============================================================================
// Window decay, gates, and sleep behaviour. Three kinds of test:
//
//   invariant        - must hold now and forever; any failure is a bug
//   boundary         - pins an off-by-one a refactor would silently move
//   CHARACTERISATION - encodes today's wrong number. Replace it when the defect
//                      it describes is fixed; the tag is greppable.
// ============================================================================

// --- the oracle -------------------------------------------------------------
//
// The definition the buckets approximate: airtime physically on air inside
// (now - window, now]. Assert against this rather than hand-worked constants.
// A packet is stamped with its END time, as completeSending() has it; the
// start is end - airtime.

struct AirtimeEvent {
    uint64_t endMs;
    uint32_t airtimeMs;
};

static float expectedUtilisation(const AirtimeEvent *ev, size_t n, uint64_t nowMs, uint32_t windowMs)
{
    const uint64_t lo = (nowMs > windowMs) ? (nowMs - windowMs) : 0;
    uint64_t busy = 0;
    for (size_t i = 0; i < n; i++) {
        const uint64_t start = (ev[i].airtimeMs < ev[i].endMs) ? (ev[i].endMs - ev[i].airtimeMs) : 0;
        const uint64_t from = start > lo ? start : lo;
        const uint64_t to = ev[i].endMs < nowMs ? ev[i].endMs : nowMs;
        if (to > from)
            busy += (to - from);
    }
    return (float)busy / (float)windowMs * 100.0f;
}

// Steady load helper: logs `msPerSecond` of airtime once a second for `seconds`,
// leaving the clock exactly `seconds` later than it started.
static void logEverySecond(AirTime &a, uint32_t seconds, uint32_t msPerSecond, reportTypes type = RX_LOG)
{
    for (uint32_t i = 0; i < seconds; i++) {
        a.logAirtime(type, msPerSecond);
        Time::advanceTestMillis(1000);
    }
}

static char g_msg[160]; // Unity messages must outlive the assert

// --- hourly period rotation: boundaries the first three tests miss -----------

// The shift loop runs PERIODS_TO_LOG-2 -> 0; an off-by-one resurrects hour-old
// data into slot 0 instead of dropping it.
void test_oldest_period_falls_off_the_end()
{
    Time::setTestMillis(0);
    AirTime a;

    for (uint32_t h = 0; h < PERIODS_TO_LOG; h++) {
        a.logAirtime(TX_LOG, (h + 1) * 100);
        Time::advanceTestMillis(3600u * 1000u);
    }

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    // Slot 0 is the (empty) current hour; 800 was the newest logged, 100 the oldest.
    TEST_ASSERT_EQUAL_UINT32(0, report[0]);
    TEST_ASSERT_EQUAL_UINT32(800, report[1]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(200, report[7], "the oldest survivor sits in the last slot");

    Time::advanceTestMillis(3600u * 1000u);
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(300, report[7], "one more hour drops 200 off the end");
    for (uint8_t i = 0; i < PERIODS_TO_LOG; i++)
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(200, report[i], "dropped data must not wrap back in");
}

void test_period_boundary_is_exact_at_one_hour()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 500);

    Time::advanceTestMillis(3599u * 1000u);
    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, report[0], "3599s must not rotate");

    Time::advanceTestMillis(1000);
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, report[0], "3600s rotates exactly once");
    TEST_ASSERT_EQUAL_UINT32(500, report[1]);
}

// The >= is the seam between "rotate N times" and "wipe the lot".
void test_period_clear_boundary_is_exactly_the_log_depth()
{
    {
        Time::setTestMillis(0);
        AirTime shift;
        shift.logAirtime(TX_LOG, 500);
        Time::advanceTestMillis(7u * 3600u * 1000u); // 7 h: shift branch
        uint32_t report[PERIODS_TO_LOG] = {0};
        TEST_ASSERT_TRUE(shift.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, report[7], "7h shifts to the last slot");
    }
    {
        Time::resetMonotonicForTests();
        Time::setTestMillis(0);
        AirTime wipe;
        wipe.logAirtime(TX_LOG, 500);
        Time::advanceTestMillis(8u * 3600u * 1000u); // 8 h: memset branch
        uint32_t report[PERIODS_TO_LOG] = {0};
        TEST_ASSERT_TRUE(wipe.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
        for (uint8_t i = 0; i < PERIODS_TO_LOG; i++)
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, report[i], "8h wipes rather than rotating");
    }
}

// --- channelUtilization: the 6 x 10 s modular ring --------------------------

// Airtime ages out oldest-first. The ring's index is absolute uptime phase, so
// the oldest bucket is (current + 1) % N, never index N-1 - the assumption
// getSilentMinutes() wrongly makes about the other ring. Stated as a property
// so it holds at any geometry.
void test_channel_utilization_ages_out_oldest_first()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(RX_LOG, 6000); // A: 10% of the window
    Time::advanceTestMillis(15u * 1000u);
    a.logAirtime(RX_LOG, 3000); // B: 5%, logged later, must outlive A

    bool sawBOnly = false;
    for (uint32_t t = 16; t <= 120; t++) {
        Time::advanceTestMillis(1000);
        const float pct = a.channelUtilizationPercent();
        // "A alone" would be 10% with B already gone: that is out-of-order ageing.
        TEST_ASSERT_FALSE_MESSAGE(pct > 9.0f && pct < 11.0f && sawBOnly, "A must not outlive B");
        if (pct > 4.0f && pct < 6.0f)
            sawBOnly = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawBOnly, "there must be a window where only the newer airtime remains");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, a.channelUtilizationPercent());
}

void test_channel_utilization_clears_only_the_buckets_crossed()
{
    Time::setTestMillis(0);
    AirTime a;

    // One distinct value per 10 s bucket: 1000, 2000, ... 6000 ms.
    for (uint32_t b = 0; b < 6; b++) {
        a.logAirtime(RX_LOG, (b + 1) * 1000);
        Time::advanceTestMillis(10u * 1000u);
    }
    // t = 60 s: bucket 0 has just been cleared, so 1000 is already gone.
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, (2000 + 3000 + 4000 + 5000 + 6000) / 600.0f, a.channelUtilizationPercent(),
                                     "entering a bucket clears exactly that bucket");

    Time::advanceTestMillis(20u * 1000u); // crosses two more
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, (4000 + 5000 + 6000) / 600.0f, a.channelUtilizationPercent(),
                                     "20s must clear exactly two buckets, oldest first");
}

void test_channel_utilization_clear_boundary_is_exactly_six_periods()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(RX_LOG, 6000);

    Time::advanceTestMillis(59u * 1000u);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 10.0f, a.channelUtilizationPercent(), "59s: still inside the window");

    Time::advanceTestMillis(1000);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, a.channelUtilizationPercent(), "60s: the bucket is reused");
}

void test_channel_utilization_is_zero_when_nothing_logged()
{
    Time::setTestMillis(0);
    AirTime a;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.channelUtilizationPercent());
    Time::advanceTestMillis(3600u * 1000u);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.channelUtilizationPercent());
}

void test_channel_utilization_decays_proportionally_across_light_sleep()
{
    Time::setTestMillis(0);
    AirTime a;
    AirtimeEvent ev[6];
    for (uint32_t b = 0; b < 6; b++) {
        a.logAirtime(RX_LOG, 1000);
        ev[b].endMs = (uint64_t)b * 10000u;
        ev[b].airtimeMs = 1000;
        Time::advanceTestMillis(10u * 1000u);
    }
    const float full = a.channelUtilizationPercent();
    TEST_ASSERT_TRUE(full > 0.0f);

    Time::advanceTestMillis(30u * 1000u); // asleep: not one call for half the window

    const float after = a.channelUtilizationPercent();
    const float truth = expectedUtilisation(ev, 6, 90000, 60000);
    snprintf(g_msg, sizeof(g_msg), "before %.4f%%, after a 30s gap %.4f%%, oracle %.4f%%", full, after, truth);
    TEST_ASSERT_TRUE_MESSAGE(after < full, g_msg);
    // Whole buckets shed, so the survivors are exactly what was still on air in
    // the last 60s.
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, truth, after, g_msg);
}

// Hold wall time and airtime fixed, vary only how often the class is polled,
// and assert the answer does not move. Fails if rotation moves back into
// runOnce() only.
void test_channel_utilization_is_independent_of_scheduler_rate()
{
    Time::setTestMillis(0);
    AirTime polledOften;
    AirTime polledOnce;

    for (uint32_t s = 0; s < 45; s++) {
        polledOften.logAirtime(RX_LOG, 200);
        polledOnce.logAirtime(RX_LOG, 200);
        Time::advanceTestMillis(1000);
        (void)polledOften.channelUtilizationPercent(); // once a second
    }

    snprintf(g_msg, sizeof(g_msg), "polled 45x: %.4f%%, polled once: %.4f%%", polledOften.channelUtilizationPercent(),
             polledOnce.channelUtilizationPercent());
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, polledOnce.channelUtilizationPercent(), polledOften.channelUtilizationPercent(),
                                     g_msg);
}

// A percentage of a fixed window cannot exceed 100. Holds for every preset
// whose packets fit inside a bucket; LONG_SLOW is characterised below.
void test_channel_utilization_never_exceeds_100_percent()
{
    Time::setTestMillis(0);
    AirTime a;

    float peak = 0.0f;
    for (uint32_t s = 0; s < 200; s++) {
        a.logAirtime(RX_LOG, 1000); // a fully saturated channel: 1000ms of airtime per second
        Time::advanceTestMillis(1000);
        const float pct = a.channelUtilizationPercent();
        if (pct > peak)
            peak = pct;
    }
    snprintf(g_msg, sizeof(g_msg), "peak reading was %.4f%%", peak);
    TEST_ASSERT_TRUE_MESSAGE(peak <= 100.01f, g_msg);
}

void test_channel_utilization_counts_each_packet_once()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(TX_LOG, 1000);
    a.logAirtime(RX_LOG, 2000);
    a.logAirtime(RX_ALL_LOG, 3000);

    // 6000ms of the 60s window, counted once each.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, a.channelUtilizationPercent());
}

// CHARACTERISATION. The current bucket is zeroed on entry and fills across its
// period, so the window covers (N-1)p + phase against a denominator of Np -
// right after a boundary, 50s of coverage divided by 60s.
void test_channel_utilization_covers_less_than_its_denominator()
{
    Time::setTestMillis(0);
    AirTime a;

    AirtimeEvent ev[61];
    size_t n = 0;
    for (uint32_t s = 0; s < 60; s++) {
        a.logAirtime(RX_LOG, 100);
        ev[n].endMs = (uint64_t)s * 1000;
        ev[n].airtimeMs = 100;
        n++;
        Time::advanceTestMillis(1000);
    }
    // t = 60 000 ms, phase 0: the bucket holding t=0..9 has just been reused.
    const float truth = expectedUtilisation(ev, n, 60000, 60000);
    const float reported = a.channelUtilizationPercent();

    snprintf(g_msg, sizeof(g_msg), "oracle %.4f%%, reported %.4f%% (deficit %.4f pp)", truth, reported, truth - reported);
    TEST_ASSERT_TRUE_MESSAGE(truth > 9.5f, g_msg); // a steady 10% load, less the event on the window edge
    TEST_ASSERT_TRUE_MESSAGE(reported < truth - 1.0f, g_msg);
}

// CHARACTERISATION. The same defect numerically: under a steady load the
// reading sweeps with position inside the current bucket instead of holding.
void test_channel_utilization_quantisation_error_by_phase()
{
    Time::setTestMillis(0);
    AirTime a;
    for (uint32_t s = 0; s < 60; s++) {
        a.logAirtime(RX_LOG, 100);
        Time::advanceTestMillis(1000);
    }

    float lo = 1000.0f, hi = 0.0f;
    for (uint32_t s = 0; s < 10; s++) { // one full bucket period of phases
        const float pct = a.channelUtilizationPercent();
        if (pct < lo)
            lo = pct;
        if (pct > hi)
            hi = pct;
        a.logAirtime(RX_LOG, 100);
        Time::advanceTestMillis(1000);
    }

    snprintf(g_msg, sizeof(g_msg), "steady 10%% load reads %.4f%%..%.4f%% across bucket phase", lo, hi);
    TEST_ASSERT_TRUE_MESSAGE(lo < 9.0f, g_msg);      // under-reports at the start of a bucket
    TEST_ASSERT_TRUE_MESSAGE(hi > 9.5f, g_msg);      // recovers by the end of it
    TEST_ASSERT_TRUE_MESSAGE(hi - lo > 1.0f, g_msg); // and the sawtooth is the jitter defect
}

// CHARACTERISATION. A packet's whole airtime is credited to the bucket it
// completed in, so a bucket can hold more than its own period. LONG_SLOW at max
// payload is 14 164 ms against a 10 s bucket.
void test_channel_utilization_exceeds_100_percent_on_long_slow()
{
    Time::setTestMillis(0);
    AirTime a;

    const uint32_t LONG_SLOW_MAX_MS = 14164;
    float peak = 0.0f;
    for (uint32_t i = 0; i < 40; i++) {
        Time::advanceTestMillis(LONG_SLOW_MAX_MS); // back-to-back: the channel is 100% busy
        a.logAirtime(RX_LOG, LONG_SLOW_MAX_MS);
        const float pct = a.channelUtilizationPercent();
        if (pct > peak)
            peak = pct;
    }

    snprintf(g_msg, sizeof(g_msg), "true occupancy 100%%, peak reading %.4f%%", peak);
    TEST_ASSERT_TRUE_MESSAGE(peak > 100.0f, g_msg);
}

// --- utilizationTX: the 60 x 60 s modular ring ------------------------------

void test_tx_utilization_ages_out_oldest_first()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(TX_LOG, 60000); // A
    Time::advanceTestMillis(15u * 60u * 1000u);
    a.logAirtime(TX_LOG, 30000); // B, newer and smaller

    bool sawBOnly = false;
    for (uint32_t m = 16; m <= 120; m++) {
        Time::advanceTestMillis(60u * 1000u);
        const float pct = a.utilizationTXPercent();
        const float bOnly = 30000.0f / (60.0f * 60.0f * 1000.0f) * 100.0f;
        TEST_ASSERT_FALSE_MESSAGE(sawBOnly && pct > bOnly * 1.5f, "A must not outlive B");
        if (pct > bOnly * 0.9f && pct < bOnly * 1.1f)
            sawBOnly = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawBOnly, "there must be a window where only the newer airtime remains");
}

void test_tx_utilization_clears_only_the_minutes_crossed()
{
    Time::setTestMillis(0);
    AirTime a;
    for (uint32_t m = 0; m < 4; m++) {
        a.logAirtime(TX_LOG, (m + 1) * 1000);
        Time::advanceTestMillis(60u * 1000u);
    }
    const float all = (1000 + 2000 + 3000 + 4000) / (float)MS_IN_HOUR * 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, all, a.utilizationTXPercent());

    Time::advanceTestMillis(56u * 60u * 1000u); // t = 60 min: the first minute-bucket is reused
    const float withoutFirst = (2000 + 3000 + 4000) / (float)MS_IN_HOUR * 100.0f;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, withoutFirst, a.utilizationTXPercent(),
                                     "only the crossed minute buckets are cleared");
}

void test_tx_utilization_clear_boundary_is_exactly_sixty_minutes()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 36000);

    Time::advanceTestMillis(59u * 60u * 1000u);
    TEST_ASSERT_TRUE_MESSAGE(a.utilizationTXPercent() > 0.0f, "59 min: still inside the hour");

    Time::advanceTestMillis(60u * 1000u);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001f, 0.0f, a.utilizationTXPercent(), "60 min: the bucket is reused");
}

void test_tx_utilization_counts_only_transmissions()
{
    Time::setTestMillis(0);
    AirTime a;

    a.logAirtime(RX_LOG, MS_IN_HOUR / 2);
    a.logAirtime(RX_ALL_LOG, MS_IN_HOUR / 2);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001f, 0.0f, a.utilizationTXPercent(),
                                     "received airtime must never reach the duty-cycle figure");

    a.logAirtime(TX_LOG, 36000);
    TEST_ASSERT_TRUE(a.utilizationTXPercent() > 0.0f);
}

// CHARACTERISATION. The same quantisation defect on the hour window: 10x
// smaller because N is 60 rather than 6, but not zero.
void test_tx_utilization_quantisation_error()
{
    Time::setTestMillis(0);
    AirTime a;
    for (uint32_t m = 0; m < 60; m++) {
        a.logAirtime(TX_LOG, 1000);
        Time::advanceTestMillis(60u * 1000u);
    }
    // 60 000 ms of TX in the hour just elapsed = 1.6667% true.
    const float truth = 60000.0f / (float)MS_IN_HOUR * 100.0f;
    const float reported = a.utilizationTXPercent();

    snprintf(g_msg, sizeof(g_msg), "true %.4f%%, reported %.4f%%", truth, reported);
    TEST_ASSERT_TRUE_MESSAGE(reported < truth, g_msg);
    TEST_ASSERT_TRUE_MESSAGE(reported > truth * 0.95f, g_msg); // ~1/60, not gross
}

// --- TX gates ----------------------------------------------------------------

void test_isTxAllowedChannelUtil_polite_threshold_is_lower()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(RX_LOG, 18000); // 30% of the 60s window

    TEST_ASSERT_TRUE_MESSAGE(a.isTxAllowedChannelUtil(false), "30% is under the 40% default");
    TEST_ASSERT_FALSE_MESSAGE(a.isTxAllowedChannelUtil(true), "30% is over the 25% polite limit");
}

// The compare is `< percentage`, so exactly the threshold must block.
void test_isTxAllowedChannelUtil_boundary_is_exclusive()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(RX_LOG, 24000); // exactly 40.0%

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 40.0f, a.channelUtilizationPercent());
    TEST_ASSERT_FALSE_MESSAGE(a.isTxAllowedChannelUtil(false), "exactly 40.0% must block, not allow");
}

void test_isTxAllowedAirUtil_allows_when_override_is_set()
{
    Time::setTestMillis(0);
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    config.lora.override_duty_cycle = true;
    initRegion();
    AirTime a;
    a.logAirtime(TX_LOG, MS_IN_HOUR); // 100% TX utilisation

    TEST_ASSERT_TRUE(a.isTxAllowedAirUtil());
    config.lora.override_duty_cycle = false;
}

void test_isTxAllowedAirUtil_allows_when_the_region_is_unlimited()
{
    Time::setTestMillis(0);
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.override_duty_cycle = false;
    initRegion();
    AirTime a;
    a.logAirtime(TX_LOG, MS_IN_HOUR);

    TEST_ASSERT_TRUE_MESSAGE(getEffectiveDutyCycle() >= 100.0f, "US has no duty cycle limit");
    TEST_ASSERT_TRUE(a.isTxAllowedAirUtil());
}

// The polite gate is half the allowance, not the whole of it.
void test_isTxAllowedAirUtil_blocks_at_half_the_duty_cycle()
{
    Time::setTestMillis(0);
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    config.lora.override_duty_cycle = false;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    initRegion();
    const float duty = getEffectiveDutyCycle(); // 2.5% for a non-router on EU_866
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, duty);

    AirTime a;
    // 40% of the allowance: under half, so still allowed.
    a.logAirtime(TX_LOG, (uint32_t)(MS_IN_HOUR * duty / 100.0f * 0.40f));
    TEST_ASSERT_TRUE_MESSAGE(a.isTxAllowedAirUtil(), "40% of the allowance is under the polite half");

    // Push past half.
    a.logAirtime(TX_LOG, (uint32_t)(MS_IN_HOUR * duty / 100.0f * 0.30f));
    TEST_ASSERT_FALSE_MESSAGE(a.isTxAllowedAirUtil(), "70% of the allowance is over the polite half");
}

// Two thresholds ride on one figure: isTxAllowedAirUtil() is polite at half the
// duty cycle, while Router::send() aborts only at the whole of it. There is a
// band where the polite gate blocks and the hard gate would not - pinning it
// here means an accuracy change has to be evaluated against both.
void test_router_send_gate_uses_the_whole_duty_cycle()
{
    Time::setTestMillis(0);
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    config.lora.override_duty_cycle = false;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    initRegion();
    const float duty = getEffectiveDutyCycle();

    AirTime a;
    a.logAirtime(TX_LOG, (uint32_t)(MS_IN_HOUR * duty / 100.0f * 0.70f)); // 70% of the allowance

    TEST_ASSERT_FALSE_MESSAGE(a.isTxAllowedAirUtil(), "the polite gate blocks at 70% of the allowance");
    TEST_ASSERT_TRUE_MESSAGE(a.utilizationTXPercent() < duty,
                             "...while the figure is still under the whole duty cycle Router::send() uses");
}

// getEffectiveDutyCycle() special-cases EU_866 by role. Every other region -
// including EU_868, one digit away - takes the generic myRegion->dutyCycle path.
void test_effective_duty_cycle_special_case_is_eu_866_only()
{
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    initRegion();
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    const float eu866Client = getEffectiveDutyCycle();
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    const float eu866Router = getEffectiveDutyCycle();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, eu866Client);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 10.0f, eu866Router, "EU_866 is role-dependent");

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    initRegion();
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    const float eu868Client = getEffectiveDutyCycle();
    config.device.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    const float eu868Router = getEffectiveDutyCycle();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, eu868Client, eu868Router, "EU_868 must NOT be role-dependent");

    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
}

// --- getSilentMinutes() ------------------------------------------------------

void test_getSilentMinutes_returns_zero_when_already_under_the_limit()
{
    Time::setTestMillis(0);
    AirTime a;
    TEST_ASSERT_EQUAL_UINT8(0, a.getSilentMinutes(1.0f, 2.5f));
}

void test_getSilentMinutes_returns_a_full_hour_when_nothing_ages_out()
{
    Time::setTestMillis(0);
    AirTime a; // empty ring, but told we are over the limit
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(60, a.getSilentMinutes(10.0f, 2.5f), "nothing to age out means the full hour");
}

void test_getSilentMinutes_counts_minutes_until_enough_ages_out()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 120000); // two minutes of TX, all of it in minute-bucket 0
    const float pct = a.utilizationTXPercent();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.3333f, pct);

    // Fully determined: the walk subtracts nothing for i in 59..1, then the whole 3.3333% at i == 0,
    // returning MINUTES_IN_HOUR - 1 - 0. That answer is one minute short of the truth - syncNow()
    // clears bucket 0 at minute 60, not 59 - which test_getSilentMinutes_depends_on_ring_phase pins.
    const uint8_t mins = a.getSilentMinutes(pct, 2.5f);
    TEST_ASSERT_EQUAL_UINT8(59, mins);
}

// CHARACTERISATION. getSilentMinutes() walks utilizationTX from index 59 down
// to 0 and returns 59 - i, treating the index as an age. That is the report
// array's convention; utilizationTX is a modular ring indexed by minute phase,
// so identical airtime gives different answers at different phases.
void test_getSilentMinutes_depends_on_ring_phase()
{
    uint8_t answers[6] = {0};
    float pcts[6] = {0};
    for (uint8_t i = 0; i < 6; i++) {
        Time::resetMonotonicForTests();
        Time::setTestMillis((uint32_t)i * 10u * 60u * 1000u); // 0, 10, 20... minutes of uptime
        AirTime a;
        a.logAirtime(TX_LOG, 120000);
        pcts[i] = a.utilizationTXPercent();
        answers[i] = a.getSilentMinutes(pcts[i], 2.5f);
    }

    for (uint8_t i = 1; i < 6; i++)
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001f, pcts[0], pcts[i], "the inputs must be identical");

    bool varies = false;
    for (uint8_t i = 1; i < 6; i++)
        if (answers[i] != answers[0])
            varies = true;

    snprintf(g_msg, sizeof(g_msg), "same airtime, answers by phase: %u %u %u %u %u %u", answers[0], answers[1], answers[2],
             answers[3], answers[4], answers[5]);
    TEST_ASSERT_TRUE_MESSAGE(varies, g_msg);
}

// --- clock robustness ---------------------------------------------------------

// A gap longer than the window that also crosses the 49.7-day millis() wrap.
void test_survives_heavy_sleep_across_the_wrap()
{
    const uint32_t beforeWrap = 0xFFFFFFFFu - (30u * 1000u);
    Time::setTestMillis(beforeWrap);
    Time::serviceMonotonic();
    AirTime a;
    a.logAirtime(RX_LOG, 6000);
    TEST_ASSERT_TRUE(a.channelUtilizationPercent() > 0.0f);

    Time::advanceTestMillis(120u * 1000u); // wraps, and outlasts the 60s window
    Time::serviceMonotonic();

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, a.channelUtilizationPercent(),
                                     "a window that outlasts its span must be empty, wrap or not");
}

void test_multi_day_sleep_clears_every_window()
{
    Time::setTestMillis(0);
    AirTime a;
    a.logAirtime(TX_LOG, 6000);
    a.logAirtime(RX_LOG, 6000);
    a.logAirtime(RX_ALL_LOG, 6000);

    Time::advanceTestMillis(3u * 24u * 3600u * 1000u); // three days
    Time::serviceMonotonic();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.channelUtilizationPercent());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, a.utilizationTXPercent());
    uint32_t report[PERIODS_TO_LOG] = {0};
    const reportTypes types[] = {TX_LOG, RX_LOG, RX_ALL_LOG};
    for (uint8_t t = 0; t < 3; t++) {
        TEST_ASSERT_TRUE(a.airtimeReport(types[t], report, PERIODS_TO_LOG));
        for (uint8_t i = 0; i < PERIODS_TO_LOG; i++)
            TEST_ASSERT_EQUAL_UINT32(0, report[i]);
    }
}

// getUptimeSecs() is monotonic by construction. If it ever stops being, the
// elapsed calculation underflows to a huge value, which trips every >= branch
// and clears the windows. Benign, and pinned so a swap back to bare millis()
// fails loudly rather than corrupting buckets.
void test_backwards_uptime_degrades_safely()
{
    // Step by the wrap, which is the size the regression would actually produce: uptime falls from
    // 4294967s to 0. A smaller backwards step leaves elapsedAirtimePeriods at 0, so the hourly
    // report below is never reached - which is what this case used to miss.
    Time::setTestMillis(UINT32_MAX);
    AirTime a;
    a.logAirtime(TX_LOG, 6000);
    TEST_ASSERT_TRUE(a.channelUtilizationPercent() > 0.0f);

    Time::setTestMillis(0); // the wrap, as a naive millis() clock would present it

    const float pct = a.channelUtilizationPercent();
    snprintf(g_msg, sizeof(g_msg), "channel utilisation after the wrap: %.4f%%", pct);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0001f, 0.0f, pct, g_msg);

    uint32_t report[PERIODS_TO_LOG] = {0};
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    for (uint32_t i = 0; i < PERIODS_TO_LOG; i++)
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, report[i], "every hourly bucket clears across the wrap");
}

// --- the lock ----------------------------------------------------------------------------------

// No single public method may take the lock twice: a second Held on the same instance trips the
// re-entry assert. The calls below are sequential and each Held is destroyed before the next, so
// this catches a method re-entering itself, not two methods nesting. That is the regression guard
// for isTxAllowedChannelUtil() regaining its pre-split shape. Two of the methods called take no
// lock at all. Portduino compiles Lock::lock() to an empty body, so the assert is the only check
// that works natively; on hardware the same bug is a deadlock.
void test_no_public_method_takes_the_lock_twice()
{
    Time::setTestMillis(0);
    // EU_868 explicitly, not inherited: isTxAllowedAirUtil() constructs a Held only inside its
    // duty-cycle branch, so under the default US region (100%) it would return before locking and
    // this test would not cover it at all.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.override_duty_cycle = false;
    initRegion();

    AirTime a;
    uint32_t report[PERIODS_TO_LOG] = {0};

    a.logAirtime(TX_LOG, 100);
    a.logAirtime(RX_LOG, 100);
    a.logAirtime(RX_ALL_LOG, 100);
    (void)a.channelUtilizationPercent();
    (void)a.utilizationTXPercent();
    a.airtimeRotatePeriod();
    (void)a.getPeriodsToLog();
    (void)a.getSecondsPerPeriod();
    (void)a.getSecondsSinceBoot();
    (void)a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG);
    (void)a.getSilentMinutes(10.0f, 2.5f);
    (void)a.isTxAllowedChannelUtil(false);
    (void)a.isTxAllowedChannelUtil(true);
    (void)a.isTxAllowedAirUtil();

    // Reaching here without the assert firing IS the assertion; check the object still works.
    TEST_ASSERT_TRUE(a.airtimeReport(TX_LOG, report, PERIODS_TO_LOG));
    TEST_ASSERT_EQUAL_UINT32(100, report[0]);
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

    // --- phase 3: windows, gates, sleep ---
    RUN_TEST(test_oldest_period_falls_off_the_end);
    RUN_TEST(test_period_boundary_is_exact_at_one_hour);
    RUN_TEST(test_period_clear_boundary_is_exactly_the_log_depth);
    RUN_TEST(test_channel_utilization_ages_out_oldest_first);
    RUN_TEST(test_channel_utilization_clears_only_the_buckets_crossed);
    RUN_TEST(test_channel_utilization_clear_boundary_is_exactly_six_periods);
    RUN_TEST(test_channel_utilization_is_zero_when_nothing_logged);
    RUN_TEST(test_channel_utilization_decays_proportionally_across_light_sleep);
    RUN_TEST(test_channel_utilization_is_independent_of_scheduler_rate);
    RUN_TEST(test_channel_utilization_never_exceeds_100_percent);
    RUN_TEST(test_channel_utilization_counts_each_packet_once);
    RUN_TEST(test_channel_utilization_covers_less_than_its_denominator);
    RUN_TEST(test_channel_utilization_quantisation_error_by_phase);
    RUN_TEST(test_channel_utilization_exceeds_100_percent_on_long_slow);
    RUN_TEST(test_tx_utilization_ages_out_oldest_first);
    RUN_TEST(test_tx_utilization_clears_only_the_minutes_crossed);
    RUN_TEST(test_tx_utilization_clear_boundary_is_exactly_sixty_minutes);
    RUN_TEST(test_tx_utilization_counts_only_transmissions);
    RUN_TEST(test_tx_utilization_quantisation_error);
    RUN_TEST(test_isTxAllowedChannelUtil_polite_threshold_is_lower);
    RUN_TEST(test_isTxAllowedChannelUtil_boundary_is_exclusive);
    RUN_TEST(test_isTxAllowedAirUtil_allows_when_override_is_set);
    RUN_TEST(test_isTxAllowedAirUtil_allows_when_the_region_is_unlimited);
    RUN_TEST(test_isTxAllowedAirUtil_blocks_at_half_the_duty_cycle);
    RUN_TEST(test_router_send_gate_uses_the_whole_duty_cycle);
    RUN_TEST(test_effective_duty_cycle_special_case_is_eu_866_only);
    RUN_TEST(test_getSilentMinutes_returns_zero_when_already_under_the_limit);
    RUN_TEST(test_getSilentMinutes_returns_a_full_hour_when_nothing_ages_out);
    RUN_TEST(test_getSilentMinutes_counts_minutes_until_enough_ages_out);
    RUN_TEST(test_getSilentMinutes_depends_on_ring_phase);
    RUN_TEST(test_survives_heavy_sleep_across_the_wrap);
    RUN_TEST(test_multi_day_sleep_clears_every_window);
    RUN_TEST(test_backwards_uptime_degrades_safely);
    RUN_TEST(test_no_public_method_takes_the_lock_twice);
    exit(UNITY_END());
}

void loop() {}
