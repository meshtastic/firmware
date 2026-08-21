#include "TestUtil.h"
#include "modules/Telemetry/AirQualityTelemetry.h"
#include "modules/Telemetry/TelemetryHistory.h"
#include <unity.h>

// These exercise AirQualityTelemetryModule's pure scheduling policy: the local device-to-phone loop
// that keeps readings near-realtime, and the offload that samples those readings onto the mesh at
// its own interval. They take plain values, so no device globals or fake clock are needed.

constexpr uint32_t LOCAL_MS = 60000U; // local loop cadence

// Nothing read yet: measure immediately, whatever the clock or the consumers say.
static void test_read_firstReadIsImmediate()
{
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldReadSensors(false, 5000U, 0U, LOCAL_MS, true, false));
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldReadSensors(false, 0U, 0U, LOCAL_MS, false, false));
}

static void test_read_holdsUntilLocalCadenceElapses()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldReadSensors(true, 59999U, 0U, LOCAL_MS, true, false));
}

static void test_read_readsWhenLocalCadenceElapses()
{
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldReadSensors(true, 60000U, 0U, LOCAL_MS, true, false));
}

// A read stamped at millis()==0 must still hold the cadence; everRead, not lastReadMs, marks the
// never-read state.
static void test_read_readAtTimeZeroStillHoldsCadence()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldReadSensors(true, 30000U, 0U, LOCAL_MS, true, false));
}

// A backed-up toPhone queue means no client is draining it; the local loop pauses rather than
// warming the sensor for nobody.
static void test_read_backedUpPhoneQueuePausesLocalLoop()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldReadSensors(true, 120000U, 0U, LOCAL_MS, false, false));
}

// ...but the offload still needs something to publish, so a due on-air send keeps the loop running.
static void test_read_offloadKeepsLoopAliveWhilePhoneQueueIsStalled()
{
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldReadSensors(true, 120000U, 0U, LOCAL_MS, false, true));
}

// The offload never reads ahead of the local cadence - it samples what the loop already took.
static void test_read_offloadDoesNotOutpaceLocalCadence()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldReadSensors(true, 30000U, 0U, LOCAL_MS, true, true));
}

// millis() rollover: unsigned subtraction keeps the elapsed math correct across the wrap.
static void test_read_survivesMillisRollover()
{
    constexpr uint32_t lastRead = UINT32_MAX - 29999U; // 30,000 ms before the wrap to 0
    // 40s past the wrap: 70,000 ms elapsed, past the 60s cadence.
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldReadSensors(true, 40000U, lastRead, LOCAL_MS, true, false));
    // 10s past the wrap: only 40,000 ms elapsed, still held.
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldReadSensors(true, 10000U, lastRead, LOCAL_MS, true, false));
}

// The offload keeps its own cadence and its own airtime veto over the latched local reading.

static void test_mesh_publishesUnsentReadingWhenDueAndAllowed()
{
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldSendToMesh(true, true, true, false));
}

// Between offloads the local loop keeps reading, but nothing goes on air.
static void test_mesh_holdsUntilDue()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(true, false, true, false));
}

static void test_mesh_holdsWhileAirtimeVetoes()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(true, true, false, false));
    // Even the power-saving sensor's sleep-arming path respects the airtime veto.
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(false, true, false, true));
}

// The same reading is never broadcast twice; once offloaded there is nothing left to publish.
static void test_mesh_doesNotResendTheSameReading()
{
    TEST_ASSERT_FALSE(AirQualityTelemetryModule::shouldSendToMesh(false, true, true, false));
}

// A power-saving SENSOR still takes the send path with nothing to send: that call is what arms its
// deep sleep, so skipping it would leave the node awake until the next interval.
static void test_mesh_powerSavingSensorRunsEvenWithNothingToSend()
{
    TEST_ASSERT_TRUE(AirQualityTelemetryModule::shouldSendToMesh(false, true, true, true));
}

// The history ring holds what the local loop measured between offloads. Exercised with a plain
// uint32_t payload - the ring itself knows nothing about telemetry types.

static void test_history_startsEmpty()
{
    TelemetryHistory<uint32_t, 3> h;
    TEST_ASSERT_TRUE(h.isEmpty());
    TEST_ASSERT_EQUAL_UINT8(0, h.size());
    // Nothing read yet is not the same as a reading nobody has taken.
    TEST_ASSERT_FALSE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH));
}

static void test_history_keepsCaptureTimeWithItsReading()
{
    TelemetryHistory<uint32_t, 3> h;
    h.push(11U, 1000U);
    h.push(22U, 1060U);
    TEST_ASSERT_EQUAL_UINT8(2, h.size());
    TEST_ASSERT_EQUAL_UINT32(22U, h.newest().metrics);
    TEST_ASSERT_EQUAL_UINT32(1060U, h.newest().time);
    // Oldest first, so the offload can walk them in the order they were taken.
    TEST_ASSERT_EQUAL_UINT32(11U, h.at(0).metrics);
    TEST_ASSERT_EQUAL_UINT32(1000U, h.at(0).time);
}

// Overwriting the oldest once full is the steady state: the offload samples the loop, it does not
// drain it.
static void test_history_wrapsAndDropsOldest()
{
    TelemetryHistory<uint32_t, 3> h;
    for (uint32_t i = 1; i <= 5; i++)
        h.push(i, 1000U + i);

    TEST_ASSERT_EQUAL_UINT8(3, h.size()); // never grows past capacity
    TEST_ASSERT_EQUAL_UINT32(3U, h.at(0).metrics);
    TEST_ASSERT_EQUAL_UINT32(4U, h.at(1).metrics);
    TEST_ASSERT_EQUAL_UINT32(5U, h.at(2).metrics);
    TEST_ASSERT_EQUAL_UINT32(5U, h.newest().metrics);
    TEST_ASSERT_EQUAL_UINT32(1005U, h.newest().time);
}

// Each consumer tracks what it has taken independently.
static void test_history_publishMarksOneChannelOnly()
{
    TelemetryHistory<uint32_t, 3> h;
    h.push(11U, 1000U);
    TEST_ASSERT_TRUE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH));
    TEST_ASSERT_TRUE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE));

    h.markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    TEST_ASSERT_FALSE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE));
    TEST_ASSERT_TRUE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH));
}

// A new reading is unpublished to everyone, even when it lands in a slot whose previous occupant
// had already been sent.
static void test_history_reusedSlotStartsUnpublished()
{
    TelemetryHistory<uint32_t, 2> h;
    h.push(11U, 1000U);
    h.markNewestPublished(TELEMETRY_PUBLISHED_MESH);
    h.markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    h.push(22U, 1060U);
    h.push(33U, 1120U); // wraps onto the slot that held 11

    TEST_ASSERT_EQUAL_UINT32(33U, h.newest().metrics);
    TEST_ASSERT_TRUE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH));
    TEST_ASSERT_TRUE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE));
}

// Marking by index is what a batched offload will walk; it must address oldest-first, not raw slots.
static void test_history_markByIndexFollowsOldestFirst()
{
    TelemetryHistory<uint32_t, 3> h;
    for (uint32_t i = 1; i <= 4; i++)
        h.push(i, 1000U + i); // ring now holds 2,3,4 with head off zero

    h.markPublished(0, TELEMETRY_PUBLISHED_MESH);
    TEST_ASSERT_EQUAL_UINT32(2U, h.at(0).metrics);
    TEST_ASSERT_TRUE(h.at(0).publishedMask & TELEMETRY_PUBLISHED_MESH);
    TEST_ASSERT_FALSE(h.at(1).publishedMask & TELEMETRY_PUBLISHED_MESH);
    TEST_ASSERT_TRUE(h.hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH));
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_read_firstReadIsImmediate);
    RUN_TEST(test_read_holdsUntilLocalCadenceElapses);
    RUN_TEST(test_read_readsWhenLocalCadenceElapses);
    RUN_TEST(test_read_readAtTimeZeroStillHoldsCadence);
    RUN_TEST(test_read_backedUpPhoneQueuePausesLocalLoop);
    RUN_TEST(test_read_offloadKeepsLoopAliveWhilePhoneQueueIsStalled);
    RUN_TEST(test_read_offloadDoesNotOutpaceLocalCadence);
    RUN_TEST(test_read_survivesMillisRollover);
    RUN_TEST(test_mesh_publishesUnsentReadingWhenDueAndAllowed);
    RUN_TEST(test_mesh_holdsUntilDue);
    RUN_TEST(test_mesh_holdsWhileAirtimeVetoes);
    RUN_TEST(test_mesh_doesNotResendTheSameReading);
    RUN_TEST(test_mesh_powerSavingSensorRunsEvenWithNothingToSend);
    RUN_TEST(test_history_startsEmpty);
    RUN_TEST(test_history_keepsCaptureTimeWithItsReading);
    RUN_TEST(test_history_wrapsAndDropsOldest);
    RUN_TEST(test_history_publishMarksOneChannelOnly);
    RUN_TEST(test_history_reusedSlotStartsUnpublished);
    RUN_TEST(test_history_markByIndexFollowsOldestFirst);
    exit(UNITY_END());
}

void loop() {}
}
