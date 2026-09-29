#include "TestUtil.h"
#include "modules/PositionModule.h"
#include <unity.h>

// These exercise PositionModule's pure broadcast-policy helpers (stationary detection and the
// interval floor). They take plain values, so no device globals or fake clock are needed.

// Coordinates sharing the top `precision` bits land in the same grid cell.
static void test_withinPrecisionCell_jitterStaysInCell()
{
    // At precision 16 the top 16 bits define the cell; the low 16 bits are GPS jitter.
    TEST_ASSERT_TRUE(PositionModule::positionWithinPrecisionCell(0x12340000, 0x22340000, 0x1234ABCD, 0x2234EF01, 16));
}

static void test_withinPrecisionCell_movingLatLeavesCell()
{
    TEST_ASSERT_FALSE(PositionModule::positionWithinPrecisionCell(0x12340000, 0x22340000, 0x12350000, 0x22340000, 16));
}

static void test_withinPrecisionCell_movingLonLeavesCell()
{
    TEST_ASSERT_FALSE(PositionModule::positionWithinPrecisionCell(0x12340000, 0x22340000, 0x12340000, 0x22350000, 16));
}

// precision 0 means position sharing is off - never treat as stationary/suppressible.
static void test_withinPrecisionCell_zeroPrecisionNeverSuppresses()
{
    TEST_ASSERT_FALSE(PositionModule::positionWithinPrecisionCell(0x12340000, 0x22340000, 0x12340000, 0x22340000, 0));
}

// Full precision (>=32): any difference matters, and identical full-precision coords still aren't
// "stationary" because there's no coarse cell to hold within.
static void test_withinPrecisionCell_fullPrecisionNeverSuppresses()
{
    TEST_ASSERT_FALSE(PositionModule::positionWithinPrecisionCell(0x12340000, 0x22340000, 0x12340000, 0x22340000, 32));
}

static void test_effectiveInterval_stationaryRaisesToFloor()
{
    TEST_ASSERT_EQUAL_UINT32(43200000U, PositionModule::effectiveBroadcastIntervalMs(60000U, true, 43200000U));
}

static void test_effectiveInterval_movingKeepsConfigured()
{
    TEST_ASSERT_EQUAL_UINT32(60000U, PositionModule::effectiveBroadcastIntervalMs(60000U, false, 43200000U));
}

// A configured interval already longer than the floor is never shortened.
static void test_effectiveInterval_longConfiguredWinsOverFloor()
{
    TEST_ASSERT_EQUAL_UINT32(50000000U, PositionModule::effectiveBroadcastIntervalMs(50000000U, true, 43200000U));
}

static void test_effectiveInterval_zeroFloorIsNoOp()
{
    TEST_ASSERT_EQUAL_UINT32(60000U, PositionModule::effectiveBroadcastIntervalMs(60000U, true, 0U));
}

// Local phone/UI play: positions are opt-in on the mesh, but our own position still streams to
// the connected phone at a fixed cadence (mirroring telemetry's local delivery).

// A never-sent state streams immediately once a valid position exists, regardless of the clock.
static void test_sendToPhone_firstSendIsImmediate()
{
    TEST_ASSERT_TRUE(PositionModule::shouldSendPositionToPhone(true, true, false, 5000U, 0U, 60000U));
    TEST_ASSERT_TRUE(PositionModule::shouldSendPositionToPhone(true, true, false, 0U, 0U, 60000U));
}

static void test_sendToPhone_holdsUntilCadenceElapses()
{
    TEST_ASSERT_FALSE(PositionModule::shouldSendPositionToPhone(true, true, true, 59999U, 1U, 60000U));
}

static void test_sendToPhone_sendsWhenCadenceElapses()
{
    TEST_ASSERT_TRUE(PositionModule::shouldSendPositionToPhone(true, true, true, 60001U, 1U, 60000U));
}

// No valid local position yet (e.g. GPS has no fix since boot): nothing to stream.
static void test_sendToPhone_requiresValidPosition()
{
    TEST_ASSERT_FALSE(PositionModule::shouldSendPositionToPhone(false, true, true, 60001U, 1U, 60000U));
}

// A backed-up toPhone queue means no client is draining it; don't pile on.
static void test_sendToPhone_requiresIdlePhoneQueue()
{
    TEST_ASSERT_FALSE(PositionModule::shouldSendPositionToPhone(true, false, true, 60001U, 1U, 60000U));
}

// millis() rollover: unsigned subtraction keeps the elapsed math correct across the wrap.
static void test_sendToPhone_survivesMillisRollover()
{
    constexpr uint32_t lastSent = UINT32_MAX - 29999U; // exactly 30,000 ms before the wrap to 0
    // "now" 40s after the wrap: 70,000 ms elapsed >= the 60s cadence, so it sends.
    TEST_ASSERT_TRUE(PositionModule::shouldSendPositionToPhone(true, true, true, 40000U, lastSent, 60000U));
    // "now" 10s after the wrap: only 40,000 ms elapsed, still held.
    TEST_ASSERT_FALSE(PositionModule::shouldSendPositionToPhone(true, true, true, 10000U, lastSent, 60000U));
}

// Regression: a send stamped at millis()==0 must still hold the cadence - the never-sent state
// is the everSentToPhone flag, not a lastSentMs==0 sentinel.
static void test_sendToPhone_sentAtTimeZeroStillHoldsCadence()
{
    TEST_ASSERT_FALSE(PositionModule::shouldSendPositionToPhone(true, true, true, 30000U, 0U, 60000U));
    TEST_ASSERT_TRUE(PositionModule::shouldSendPositionToPhone(true, true, true, 60000U, 0U, 60000U));
}

// copyAltitudeFields() carries each altitude field's presence from localPosition into the broadcast.
// A phone that sends a fix without an altitude, or a GGA with an empty altitude field, leaves the
// field absent; forcing presence on broadcast that as a reading of 0 m.
static constexpr uint32_t kAltMsl = meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE |
                                    meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL |
                                    meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION;
static constexpr uint32_t kAltHae =
    meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION;

static void test_copyAltitude_absentStaysAbsent()
{
    const meshtastic_Position from = meshtastic_Position_init_default;
    meshtastic_Position msl = meshtastic_Position_init_default;
    meshtastic_Position hae = meshtastic_Position_init_default;
    PositionModule::copyAltitudeFields(msl, from, kAltMsl);
    PositionModule::copyAltitudeFields(hae, from, kAltHae);
    TEST_ASSERT_FALSE(msl.has_altitude);
    TEST_ASSERT_FALSE(msl.has_altitude_geoidal_separation);
    TEST_ASSERT_FALSE(hae.has_altitude_hae);
}

// 0 m is a reading: presence, not the value, decides whether it goes out.
static void test_copyAltitude_presentZeroStaysPresent()
{
    meshtastic_Position from = meshtastic_Position_init_default;
    from.has_altitude = true;
    meshtastic_Position to = meshtastic_Position_init_default;
    PositionModule::copyAltitudeFields(to, from, kAltMsl);
    TEST_ASSERT_TRUE(to.has_altitude);
    TEST_ASSERT_EQUAL_INT32(0, to.altitude);
}

static void test_copyAltitude_flagsPickMslOrHae()
{
    meshtastic_Position from = meshtastic_Position_init_default;
    from.has_altitude = true;
    from.altitude = 545;
    from.has_altitude_hae = true;
    from.altitude_hae = 591;
    from.has_altitude_geoidal_separation = true;
    from.altitude_geoidal_separation = 46;

    meshtastic_Position msl = meshtastic_Position_init_default;
    PositionModule::copyAltitudeFields(msl, from, kAltMsl);
    TEST_ASSERT_TRUE(msl.has_altitude);
    TEST_ASSERT_EQUAL_INT32(545, msl.altitude);
    TEST_ASSERT_FALSE(msl.has_altitude_hae);
    TEST_ASSERT_EQUAL_INT32(46, msl.altitude_geoidal_separation);

    meshtastic_Position hae = meshtastic_Position_init_default;
    PositionModule::copyAltitudeFields(hae, from, kAltHae);
    TEST_ASSERT_FALSE(hae.has_altitude);
    TEST_ASSERT_TRUE(hae.has_altitude_hae);
    TEST_ASSERT_EQUAL_INT32(591, hae.altitude_hae);
}

static void test_copyAltitude_noAltitudeFlagCopiesNothing()
{
    meshtastic_Position from = meshtastic_Position_init_default;
    from.has_altitude = true;
    from.altitude = 545;
    meshtastic_Position to = meshtastic_Position_init_default;
    PositionModule::copyAltitudeFields(to, from, meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL);
    TEST_ASSERT_FALSE(to.has_altitude);
}

// groundSpeedKmhToCmPerSec() in src/modules/PositionModule.cpp feeds TAKPacketV2.speed in allocAtakPli().
// Position.ground_speed is km/h (GPS.cpp stores reader.speed.kmph()) and TAKPacketV2.speed is cm/s
// (atak.proto; TAKPacket-SDK divides by 100 to write CoT <track speed> in m/s). Guards against the raw
// km/h value landing in the cm/s field again, which a cm/s decoder reads as ~28x too slow.

static void test_groundSpeedKmhToCmPerSec_zeroStaysZero()
{
    TEST_ASSERT_EQUAL_UINT32(0U, PositionModule::groundSpeedKmhToCmPerSec(0U));
}

static void test_groundSpeedKmhToCmPerSec_exactMultiples()
{
    TEST_ASSERT_EQUAL_UINT32(250U, PositionModule::groundSpeedKmhToCmPerSec(9U));
    TEST_ASSERT_EQUAL_UINT32(1000U, PositionModule::groundSpeedKmhToCmPerSec(36U));
}

// 1 km/h is 27.78 cm/s and 100 km/h is 2777.78 cm/s: round to nearest, not truncate.
static void test_groundSpeedKmhToCmPerSec_roundsToNearest()
{
    TEST_ASSERT_EQUAL_UINT32(28U, PositionModule::groundSpeedKmhToCmPerSec(1U));
    TEST_ASSERT_EQUAL_UINT32(2778U, PositionModule::groundSpeedKmhToCmPerSec(100U));
}

// kmh * 250 overflows 32 bits above ~17M km/h; the result saturates instead of wrapping.
static void test_groundSpeedKmhToCmPerSec_saturatesAtMax()
{
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, PositionModule::groundSpeedKmhToCmPerSec(UINT32_MAX));
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_withinPrecisionCell_jitterStaysInCell);
    RUN_TEST(test_withinPrecisionCell_movingLatLeavesCell);
    RUN_TEST(test_withinPrecisionCell_movingLonLeavesCell);
    RUN_TEST(test_withinPrecisionCell_zeroPrecisionNeverSuppresses);
    RUN_TEST(test_withinPrecisionCell_fullPrecisionNeverSuppresses);
    RUN_TEST(test_effectiveInterval_stationaryRaisesToFloor);
    RUN_TEST(test_effectiveInterval_movingKeepsConfigured);
    RUN_TEST(test_effectiveInterval_longConfiguredWinsOverFloor);
    RUN_TEST(test_effectiveInterval_zeroFloorIsNoOp);
    RUN_TEST(test_sendToPhone_firstSendIsImmediate);
    RUN_TEST(test_sendToPhone_holdsUntilCadenceElapses);
    RUN_TEST(test_sendToPhone_sendsWhenCadenceElapses);
    RUN_TEST(test_sendToPhone_requiresValidPosition);
    RUN_TEST(test_sendToPhone_requiresIdlePhoneQueue);
    RUN_TEST(test_sendToPhone_survivesMillisRollover);
    RUN_TEST(test_sendToPhone_sentAtTimeZeroStillHoldsCadence);
    RUN_TEST(test_copyAltitude_absentStaysAbsent);
    RUN_TEST(test_copyAltitude_presentZeroStaysPresent);
    RUN_TEST(test_copyAltitude_flagsPickMslOrHae);
    RUN_TEST(test_copyAltitude_noAltitudeFlagCopiesNothing);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_zeroStaysZero);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_exactMultiples);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_roundsToNearest);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_saturatesAtMax);
    exit(UNITY_END());
}

void loop() {}
}
