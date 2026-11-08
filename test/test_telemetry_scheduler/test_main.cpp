#include "TestUtil.h"
#include "modules/Telemetry/TelemetryScheduler.h"
#include "modules/Telemetry/TelemetrySensorPower.h"
#include <unity.h>
#include <vector>

// Under test:
//  - TelemetryScheduler (src/modules/Telemetry/TelemetryScheduler.h): when a telemetry module reads,
//    stores and publishes. Stored readings follow the module's read interval; a connected phone adds
//    reads at its own cadence that are not stored; the mesh publishes on its own interval and backs
//    off when it is due but nothing goes out. Its stamps survive a deep sleep through Retained.
//  - prepareSensorsForRead() / sleepSensorsUntilNextRead() / sensorWakeLeadMs()
//    (src/modules/Telemetry/TelemetrySensorPower.h): a sensor that can sleep is woken its own
//    wakeUpTimeMs() ahead of the read, the read waits for the slowest one, and after the read a
//    sensor is slept only if it can warm up again before the next read.
//
// Regressions guarded: reads drifting by the warm-up each cycle, or happening on a cold sensor;
// phone cadence filling the store and multiplying mesh/MQTT traffic; a sensor slept when it cannot
// warm up in time (it would never produce a reading); a busy loop while airtime blocks the mesh;
// deltaSecs lost or doubled across deep sleep.

static constexpr uint32_t READ_MS = 360000U;  // 6 min
static constexpr uint32_t PHONE_MS = 60000U;  // 1 min
static constexpr uint32_t MESH_MS = 1800000U; // 30 min

static TelemetryCadence cadence(uint32_t mqttMs = 0)
{
    return {READ_MS, PHONE_MS, MESH_MS, mqttMs};
}

// Power-saving SENSOR: the publishes ride on the read wake-ups
static TelemetryCadence sleepyCadence()
{
    TelemetryCadence c = {READ_MS, PHONE_MS, MESH_MS, MESH_MS};
    c.publishOnReads = true;
    return c;
}

// ------------------------------------------------------------------ read cadence

static void test_scheduler_firstReadIsImmediate()
{
    TelemetryScheduler s;
    TEST_ASSERT_EQUAL_UINT32(0, s.msUntilRead(5000U, cadence(), false));
    TEST_ASSERT_TRUE(s.readIsStored(5000U, cadence()));
}

static void test_scheduler_readFollowsReadInterval()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 1000U, s.msUntilRead(2000U, cadence(), false));
    TEST_ASSERT_EQUAL_UINT32(0, s.msUntilRead(1000U + READ_MS, cadence(), false));
}

// The phone cadence only counts while a client is connected, and those reads are not stored.
static void test_scheduler_phoneAddsUnstoredReadsWhileConnected()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    const uint32_t now = 1000U + PHONE_MS;
    TEST_ASSERT_EQUAL_UINT32(0, s.msUntilRead(now, cadence(), true));
    TEST_ASSERT_FALSE(s.readIsStored(now, cadence()));
    TEST_ASSERT_EQUAL_UINT32(READ_MS - PHONE_MS, s.msUntilRead(now, cadence(), false));

    s.onRead(now, false, true);
    // A phone read moves the phone cadence, not the stored one
    TEST_ASSERT_EQUAL_UINT32(PHONE_MS, s.msUntilRead(now, cadence(), true));
    TEST_ASSERT_EQUAL_UINT32(READ_MS - PHONE_MS, s.msUntilRead(now, cadence(), false));
}

// With a phone connected the stored read still lands on its own deadline, not on the next phone tick
static void test_scheduler_storedReadKeepsItsDeadlineUnderPhoneCadence()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    s.onRead(1000U + READ_MS - 5000U, false, true); // phone read 5 s before the stored deadline
    TEST_ASSERT_EQUAL_UINT32(5000U, s.msUntilRead(1000U + READ_MS - 5000U, cadence(), true));
    TEST_ASSERT_TRUE(s.readIsStored(1000U + READ_MS, cadence()));
}

// A failed stored read still moves the cadence (no retry storm) but not the deltaSecs anchor
static void test_scheduler_failedReadKeepsDeltaAnchor()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    s.onRead(1000U + READ_MS, true, false);
    TEST_ASSERT_EQUAL_UINT32(READ_MS, s.msUntilRead(1000U + READ_MS, cadence(), false));
    TEST_ASSERT_EQUAL_UINT32(2 * READ_MS / 1000U, s.deltaSecs(1000U + 2 * READ_MS));
}

static void test_scheduler_deltaUnknownBeforeFirstStoredReading()
{
    TelemetryScheduler s;
    TEST_ASSERT_EQUAL_UINT32(0, s.deltaSecs(5000U));
    s.onRead(5000U, false, true); // phone-only reads do not anchor it
    TEST_ASSERT_EQUAL_UINT32(0, s.deltaSecs(65000U));
}

// A read that finishes late keeps the next deadline on the grid; deltaSecs still reports the real gap
static void test_scheduler_storedReadsStayOnGrid()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true, READ_MS);
    const uint32_t late = 1000U + READ_MS + 2500U; // 2.5 s late (warm-up, I2C)
    // The module stamps the reading with deltaSecs() before recording it
    TEST_ASSERT_EQUAL_UINT32(READ_MS / 1000U + 2U, s.deltaSecs(late));
    s.onRead(late, true, true, READ_MS);
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 2500U, s.msUntilRead(late, cadence(), false));
}

// More than an interval late (e.g. after a long sleep or a blocked node): restart the grid from now
static void test_scheduler_veryLateReadRestartsGrid()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true, READ_MS);
    const uint32_t late = 1000U + 2 * READ_MS + 5000U;
    s.onRead(late, true, true, READ_MS);
    TEST_ASSERT_EQUAL_UINT32(READ_MS, s.msUntilRead(late, cadence(), false));
}

// A module starting its grid joins another module's phase, so both read in the same wake-up
static void test_scheduler_restartedGridJoinsAnchorPhase()
{
    TelemetryScheduler env, aq;
    env.onRead(28000U, true, true, READ_MS);
    aq.onRead(45000U, true, true, READ_MS, env.lastStoredMs);
    TEST_ASSERT_EQUAL_UINT32(env.msUntilRead(50000U, cadence(), false), aq.msUntilRead(50000U, cadence(), false));
    // The shortened first gap is reported as it was
    const uint32_t next = 28000U + READ_MS + 3000U;
    TEST_ASSERT_EQUAL_UINT32((next - 45000U) / 1000U, aq.deltaSecs(next));
    // and the grid stays in phase
    aq.onRead(next, true, true, READ_MS, env.lastStoredMs);
    TEST_ASSERT_EQUAL_UINT32(28000U + READ_MS, aq.lastStoredMs);
}

// A late read restarts on the anchor's grid too, not from whenever it happened
static void test_scheduler_veryLateReadRejoinsAnchor()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true, READ_MS);
    const uint32_t late = 1000U + 2 * READ_MS + 5000U;
    s.onRead(late, true, true, READ_MS, 20000U);
    // the anchor's slot before the late read; the next one comes 14 s later
    TEST_ASSERT_EQUAL_UINT32(20000U + READ_MS, s.lastStoredMs);
    TEST_ASSERT_EQUAL_UINT32(14000U, s.msUntilRead(late, cadence(), false));
}

// The mesh holds for a stored read due within MESH_ALIGN_MS, so the batch carries the newest reading
static void test_scheduler_meshWaitsForImminentRead()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true, READ_MS);
    const uint32_t now = 1000U + READ_MS - 2000U;
    TEST_ASSERT_TRUE(s.storedReadSoon(now, cadence()));
    TEST_ASSERT_FALSE(s.storedReadSoon(1000U + READ_MS - TelemetryScheduler::MESH_ALIGN_MS - 1U, cadence()));
    TEST_ASSERT_FALSE(s.storedReadSoon(1000U + READ_MS, cadence())); // due now: the read runs first anyway
    // A mesh already due waits for that read instead of reporting 0 ms (no 1 s polling meanwhile)
    TEST_ASSERT_EQUAL_UINT32(2000U, s.msUntilNextAction(now, cadence(), false, 0U, 0U));
}

// A stamp taken at millis() == 0 must not read as "never"
static void test_scheduler_readAtTimeZeroStillCounts()
{
    TelemetryScheduler s;
    s.onRead(0U, true, true);
    TEST_ASSERT_NOT_EQUAL(0, s.msUntilRead(1000U, cadence(), false));
}

static void test_scheduler_survivesMillisRollover()
{
    TelemetryScheduler s;
    const uint32_t last = UINT32_MAX - 29999U;
    s.onRead(last, true, true);
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 70000U, s.msUntilRead(40000U, cadence(), false));
    TEST_ASSERT_EQUAL_UINT32(70U, s.deltaSecs(40000U));
}

// ------------------------------------------------------------------ mesh

static void test_scheduler_meshDueOnItsInterval()
{
    TelemetryScheduler s;
    TEST_ASSERT_TRUE(s.meshDue(1000U, 0U, cadence())); // never sent
    TEST_ASSERT_FALSE(s.meshDue(1000U + MESH_MS - 1U, 1000U, cadence()));
    TEST_ASSERT_TRUE(s.meshDue(1000U + MESH_MS, 1000U, cadence()));
}

// Due but blocked by airtime: wait MESH_RETRY_MS instead of retrying every poll
static void test_scheduler_meshBacksOffWhenNothingWentOut()
{
    TelemetryScheduler s;
    const uint32_t now = 1000U + MESH_MS;
    s.onMeshSkipped(now, TelemetryScheduler::MESH_RETRY_MS);
    TEST_ASSERT_FALSE(s.meshDue(now + 1000U, 1000U, cadence()));
    TEST_ASSERT_EQUAL_UINT32(TelemetryScheduler::MESH_RETRY_MS, s.msUntilMesh(now, 1000U, MESH_MS));
    TEST_ASSERT_TRUE(s.meshDue(now + TelemetryScheduler::MESH_RETRY_MS, 1000U, cadence()));
    s.onMeshSent(now);
    TEST_ASSERT_TRUE(s.meshDue(now, 1000U, cadence()));
}

// Due with nothing new: wait for the next stored reading, not a 60 s poll that would keep waking a
// power-saving SENSOR whose read interval is longer than its mesh interval
static void test_scheduler_meshEmptyWaitsForNextStoredReading()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    const uint32_t now = 1000U + 60000U;
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 60000U, s.meshRetryWhenEmpty(now, cadence()));
    s.onMeshSkipped(now, s.meshRetryWhenEmpty(now, cadence()));
    TEST_ASSERT_FALSE(s.meshDue(now + READ_MS - 60001U, 0U, cadence()));
    TEST_ASSERT_TRUE(s.meshDue(now + READ_MS - 60000U, 0U, cadence()));
    // Never shorter than the airtime retry
    TEST_ASSERT_EQUAL_UINT32(TelemetryScheduler::MESH_RETRY_MS, s.meshRetryWhenEmpty(1000U + READ_MS - 1000U, cadence()));
}

// ------------------------------------------------------------------ next action

static void test_scheduler_nextActionSubtractsWarmUpLead()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    // mesh sent just now, so the read is next: wake 30 s early for the sensor
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 30000U, s.msUntilNextAction(1000U, cadence(), false, 1000U, 30000U));
    // lead longer than the wait: due now
    TEST_ASSERT_EQUAL_UINT32(0, s.msUntilNextAction(1000U + READ_MS - 10000U, cadence(), false, 1000U, 30000U));
}

static void test_scheduler_nextActionTakesEarliestOfReadMeshMqtt()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true);
    s.onMqttPublished(1000U);
    // mesh due in 60 s beats the read
    const uint32_t lastMesh = 1000U + 60000U - MESH_MS;
    TEST_ASSERT_EQUAL_UINT32(60000U, s.msUntilNextAction(1000U, cadence(), false, lastMesh, 0U));
    // mqtt every 2 min beats a mesh sent just now
    TEST_ASSERT_EQUAL_UINT32(120000U, s.msUntilNextAction(1000U, cadence(120000U), false, 1000U, 0U));
}

// Power saving: only reads wake the node; a publish due before the next read waits for it, and one
// due within PUBLISH_SLACK_MS of a read wake-up goes out in it
static void test_scheduler_publishesRideOnReadsWhenSleepy()
{
    TelemetryScheduler s;
    s.onRead(1000U, true, true, READ_MS);
    s.onMqttPublished(1000U);
    const uint32_t lastMesh = 1000U + 60000U - MESH_MS; // mesh due in 60 s
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 30000U, s.msUntilNextAction(1000U, sleepyCadence(), false, lastMesh, 30000U));

    // Wake-ups drift: a publish a few seconds short of due goes with this read, not the next one
    const uint32_t read5 = 1000U + 5 * READ_MS;
    TEST_ASSERT_TRUE(s.meshDue(read5, 3000U, sleepyCadence()));
    TEST_ASSERT_FALSE(s.meshDue(read5, 3000U, cadence()));
    TEST_ASSERT_TRUE(s.mqttDue(read5 - 2000U, sleepyCadence()));
    TEST_ASSERT_FALSE(s.mqttDue(read5 - 2000U - TelemetryScheduler::PUBLISH_SLACK_MS, sleepyCadence()));
}

// ------------------------------------------------------------------ deep sleep retention

// The cadence and deltaSecs carry across a sleep that restarts millis() from 0
static void test_scheduler_retainedAcrossDeepSleep()
{
    TelemetryScheduler before;
    before.onRead(100000U, true, true);
    before.onMqttPublished(100000U);
    before.onMeshSent(100000U);

    TelemetryScheduler::Retained r = {};
    before.saveForSleep(r, 110000U, 0U);

    // 300 s measured between the save and the restore, whatever the planned sleep was
    TelemetryScheduler after;
    const uint32_t bootNow = 2000U;
    TEST_ASSERT_TRUE(after.restoreAfterSleep(r, bootNow, 300000U));
    // 10 s awake + 300 s away since the read
    TEST_ASSERT_EQUAL_UINT32(310U, after.deltaSecs(bootNow));
    TEST_ASSERT_EQUAL_UINT32(READ_MS - 310000U, after.msUntilRead(bootNow, cadence(), false));
    // The last mesh send too: TransmitHistory may only have a boot-relative stamp for it
    TEST_ASSERT_EQUAL_UINT32(310000U, bootNow - after.lastMeshMs(0U));
    TEST_ASSERT_EQUAL_UINT32(MESH_MS - 310000U, after.msUntilMesh(bootNow, after.lastMeshMs(0U), MESH_MS));
    // Consumed: a later cold boot cannot replay it
    TelemetryScheduler again;
    TEST_ASSERT_FALSE(again.restoreAfterSleep(r, bootNow, 300000U));
}

static void test_scheduler_retainedNeverStaysNever()
{
    TelemetryScheduler before;
    TelemetryScheduler::Retained r = {};
    before.saveForSleep(r, 5000U, 0U);
    TelemetryScheduler after;
    TEST_ASSERT_TRUE(after.restoreAfterSleep(r, 1000U, 60000U));
    TEST_ASSERT_EQUAL_UINT32(0, after.msUntilRead(1000U, cadence(), false));
    TEST_ASSERT_EQUAL_UINT32(0, after.deltaSecs(1000U));
}

// ------------------------------------------------------------------ sensor warm-up and sleep

struct FakeSensor {
    const char *sensorName = "fake";
    bool sleepy = true;
    bool active = false;
    int32_t warmUpMs = 30000;
    int32_t pendingMs = 0;
    int wakes = 0, sleeps = 0;
    bool idleWhenReady = false; // models a cleaning cycle that ends with the sensor idle

    bool canSleep() { return sleepy; }
    bool isActive() { return active; }
    int32_t wakeUpTimeMs() { return warmUpMs; }
    int32_t pendingForReadyMs()
    {
        if (active && pendingMs == 0 && idleWhenReady) {
            idleWhenReady = false;
            active = false;
        }
        return active ? pendingMs : 0;
    }
    uint32_t wakeUp()
    {
        active = true;
        pendingMs = warmUpMs;
        wakes++;
        return warmUpMs;
    }
    void sleep()
    {
        active = false;
        sleeps++;
    }
};

static void test_sensors_wokenOnlyWithinTheirLead()
{
    FakeSensor slow, fast;
    fast.warmUpMs = 5000;
    std::vector<FakeSensor *> sensors = {&slow, &fast};

    // 60 s before the read: nobody yet, come back when the slow one is due
    TEST_ASSERT_EQUAL_UINT32(30000U, prepareSensorsForRead(sensors, 60000U));
    TEST_ASSERT_EQUAL_INT(0, slow.wakes);

    // 30 s before: the slow one wakes, the fast one waits another 25 s
    TEST_ASSERT_EQUAL_UINT32(25000U, prepareSensorsForRead(sensors, 30000U));
    TEST_ASSERT_EQUAL_INT(1, slow.wakes);
    TEST_ASSERT_EQUAL_INT(0, fast.wakes);

    TEST_ASSERT_EQUAL_UINT32(5000U, prepareSensorsForRead(sensors, 5000U));
    TEST_ASSERT_EQUAL_INT(1, fast.wakes);
}

// At the read, wait for the slowest sensor that is still warming up
static void test_sensors_readWaitsForSlowest()
{
    FakeSensor a, b;
    std::vector<FakeSensor *> sensors = {&a, &b};
    a.active = b.active = true;
    a.pendingMs = 3000;
    b.pendingMs = 8000;
    TEST_ASSERT_EQUAL_UINT32(8000U, prepareSensorsForRead(sensors, 0U));
    a.pendingMs = b.pendingMs = 0;
    TEST_ASSERT_EQUAL_UINT32(0U, prepareSensorsForRead(sensors, 0U));
}

// Woken late (e.g. right after boot): the read waits out the whole warm-up, never reads cold
static void test_sensors_lateWakeWaitsFullWarmUp()
{
    FakeSensor a;
    std::vector<FakeSensor *> sensors = {&a};
    TEST_ASSERT_EQUAL_UINT32(30000U, prepareSensorsForRead(sensors, 0U));
    TEST_ASSERT_EQUAL_INT(1, a.wakes);
}

// A sensor that drops to idle when its readiness is polled (SEN5X after fan cleaning) is woken
// again and waited for, instead of being read while stopped
static void test_sensors_rewokenWhenIdleAfterMaintenance()
{
    FakeSensor a;
    a.active = true;
    a.pendingMs = 0;
    a.idleWhenReady = true;
    std::vector<FakeSensor *> sensors = {&a};
    TEST_ASSERT_EQUAL_UINT32(30000U, prepareSensorsForRead(sensors, 0U));
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_EQUAL_INT(1, a.wakes);
}

static void test_sensors_withoutSleepSupportUntouched()
{
    FakeSensor a;
    a.sleepy = false;
    a.active = true;
    std::vector<FakeSensor *> sensors = {&a};
    TEST_ASSERT_EQUAL_UINT32(0U, prepareSensorsForRead(sensors, 0U));
    sleepSensorsUntilNextRead(sensors, 600000U);
    TEST_ASSERT_EQUAL_INT(0, a.wakes);
    TEST_ASSERT_EQUAL_INT(0, a.sleeps);
    TEST_ASSERT_EQUAL_UINT32(0U, sensorWakeLeadMs(sensors));
}

// Slept only when it can warm up again before the next read
static void test_sensors_sleptOnlyWhenNextReadOutlastsWarmUp()
{
    FakeSensor a;
    a.active = true;
    std::vector<FakeSensor *> sensors = {&a};
    sleepSensorsUntilNextRead(sensors, 30000U); // equal: would not be ready in time
    TEST_ASSERT_EQUAL_INT(0, a.sleeps);
    sleepSensorsUntilNextRead(sensors, 30001U);
    TEST_ASSERT_EQUAL_INT(1, a.sleeps);
}

static void test_sensors_leadIsSlowestSleepingSensor()
{
    FakeSensor a, b, awake;
    b.warmUpMs = 45000;
    awake.warmUpMs = 90000;
    awake.active = true; // already warm, needs no lead
    std::vector<FakeSensor *> sensors = {&a, &b, &awake};
    TEST_ASSERT_EQUAL_UINT32(45000U, sensorWakeLeadMs(sensors));
}

void setUp(void) {}
void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_scheduler_firstReadIsImmediate);
    RUN_TEST(test_scheduler_readFollowsReadInterval);
    RUN_TEST(test_scheduler_phoneAddsUnstoredReadsWhileConnected);
    RUN_TEST(test_scheduler_storedReadKeepsItsDeadlineUnderPhoneCadence);
    RUN_TEST(test_scheduler_failedReadKeepsDeltaAnchor);
    RUN_TEST(test_scheduler_deltaUnknownBeforeFirstStoredReading);
    RUN_TEST(test_scheduler_storedReadsStayOnGrid);
    RUN_TEST(test_scheduler_veryLateReadRestartsGrid);
    RUN_TEST(test_scheduler_restartedGridJoinsAnchorPhase);
    RUN_TEST(test_scheduler_veryLateReadRejoinsAnchor);
    RUN_TEST(test_scheduler_meshWaitsForImminentRead);
    RUN_TEST(test_scheduler_readAtTimeZeroStillCounts);
    RUN_TEST(test_scheduler_survivesMillisRollover);
    RUN_TEST(test_scheduler_meshDueOnItsInterval);
    RUN_TEST(test_scheduler_meshBacksOffWhenNothingWentOut);
    RUN_TEST(test_scheduler_meshEmptyWaitsForNextStoredReading);
    RUN_TEST(test_scheduler_nextActionSubtractsWarmUpLead);
    RUN_TEST(test_scheduler_nextActionTakesEarliestOfReadMeshMqtt);
    RUN_TEST(test_scheduler_publishesRideOnReadsWhenSleepy);
    RUN_TEST(test_scheduler_retainedAcrossDeepSleep);
    RUN_TEST(test_scheduler_retainedNeverStaysNever);
    RUN_TEST(test_sensors_wokenOnlyWithinTheirLead);
    RUN_TEST(test_sensors_readWaitsForSlowest);
    RUN_TEST(test_sensors_lateWakeWaitsFullWarmUp);
    RUN_TEST(test_sensors_rewokenWhenIdleAfterMaintenance);
    RUN_TEST(test_sensors_withoutSleepSupportUntouched);
    RUN_TEST(test_sensors_sleptOnlyWhenNextReadOutlastsWarmUp);
    RUN_TEST(test_sensors_leadIsSlowestSleepingSensor);
    exit(UNITY_END());
}

void loop() {}
}
