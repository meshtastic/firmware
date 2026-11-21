#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/atak.pb.h"
#include "modules/PositionModule.h"
#include <cstring>
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

// The PLI itself: allocAtakPli() must put the converted value in TAKPacketV2.speed. The helper tests
// above would still pass with the raw km/h copied into the field, so this one decodes the packet.
class AtakPliProbe : public PositionModule
{
  public:
    using PositionModule::allocAtakPli;
};

static void test_allocAtakPli_speedIsCmPerSec()
{
    NodeDB *savedNodeDB = nodeDB;
    Router *savedRouter = router;
    meshtastic_LocalConfig savedConfig = config;
    meshtastic_LocalModuleConfig savedModuleConfig = moduleConfig;
    meshtastic_User savedOwner = owner;
    meshtastic_MyNodeInfo savedMyNodeInfo = myNodeInfo;
    meshtastic_Position savedLocalPosition = localPosition;

    nodeDB = new NodeDB(); // its constructor loads the globals, so they are set afterwards
    router = new Router();
    memset(&config, 0, sizeof(config));
    memset(&moduleConfig, 0, sizeof(moduleConfig));
    memset(&owner, 0, sizeof(owner));
    memset(&myNodeInfo, 0, sizeof(myNodeInfo));
    myNodeInfo.my_node_num = 0x11111111;
    localPosition = meshtastic_Position_init_zero;
    localPosition.has_ground_speed = true;
    localPosition.ground_speed = 100; // km/h
    AtakPliProbe *probe = new AtakPliProbe();

    meshtastic_MeshPacket *mp = probe->allocAtakPli();
    meshtastic_TAKPacketV2 tak = meshtastic_TAKPacketV2_init_zero;
    bool decoded = false;
    meshtastic_PortNum portnum = meshtastic_PortNum_UNKNOWN_APP;
    uint8_t flags = 0;
    if (mp) {
        portnum = mp->decoded.portnum;
        flags = mp->decoded.payload.bytes[0];
        decoded =
            pb_decode_from_bytes(mp->decoded.payload.bytes + 1, mp->decoded.payload.size - 1, &meshtastic_TAKPacketV2_msg, &tak);
        packetPool.release(mp);
    }

    // Globals are restored before asserting: a failed assertion longjmps past anything after it.
    delete probe;
    delete router;
    delete nodeDB;
    nodeDB = savedNodeDB;
    router = savedRouter;
    config = savedConfig;
    moduleConfig = savedModuleConfig;
    owner = savedOwner;
    myNodeInfo = savedMyNodeInfo;
    localPosition = savedLocalPosition;

    TEST_ASSERT_NOT_NULL(mp);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_ATAK_PLUGIN_V2, portnum);
    TEST_ASSERT_EQUAL_HEX8(0xFF, flags);
    TEST_ASSERT_TRUE(decoded);
    TEST_ASSERT_EQUAL_UINT32(2778U, tak.speed); // 100 km/h in cm/s, not the raw 100
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
    RUN_TEST(test_groundSpeedKmhToCmPerSec_zeroStaysZero);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_exactMultiples);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_roundsToNearest);
    RUN_TEST(test_groundSpeedKmhToCmPerSec_saturatesAtMax);
    RUN_TEST(test_allocAtakPli_speedIsCmPerSec);
    exit(UNITY_END());
}

void loop() {}
}
