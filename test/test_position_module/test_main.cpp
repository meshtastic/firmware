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
    exit(UNITY_END());
}

void loop() {}
}
