#include "MeshTypes.h"
#include "NextHopRouter.h"
#include "TestUtil.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void test_base5_progression()
{
    TEST_ASSERT_EQUAL_UINT8(6, computeDesiredRetransmissionCodingRate(5, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(5, 2));
}

static void test_base6_progression()
{
    TEST_ASSERT_EQUAL_UINT8(7, computeDesiredRetransmissionCodingRate(6, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(6, 2));
}

static void test_base7_or_higher_progression()
{
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(7, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(7, 2));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(8, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(8, 2));
}

static void test_second_or_later_retransmission_forces_eight()
{
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(5, 3));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(6, 4));
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_base5_progression);
    RUN_TEST(test_base6_progression);
    RUN_TEST(test_base7_or_higher_progression);
    RUN_TEST(test_second_or_later_retransmission_forces_eight);
    exit(UNITY_END());
}

void loop() {}
