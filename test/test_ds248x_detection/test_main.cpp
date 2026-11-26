// Pins matchesDs248xResetStatus() in src/detect/DS248XDetection.h, which ScanI2CTwoWire uses at all eight
// addresses a DS2482 shares with other parts (0x18-0x1F). The scan writes 0xF0, a DS248X Device Reset, and reads
// the status register back: a real DS248X then reports RST (bit 4). The check used to be `& 0x16`, which also took
// the PPD (bit 1) and SD (bit 2) bits as proof, so a T-Deck Pro's CST328 touch controller at 0x1A answering 0x2c was
// logged as a DS2482-800 and never reached the touch-controller checks. Relaxing the predicate back to any of those
// bits, or dropping the 0xFF guard against a floating bus, brings that misdetection back.
#include "MeshTypes.h"
#include "TestUtil.h"
#include "detect/DS248XDetection.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_accepts_reset_status_with_either_line_level()
{
    // LL (bit 3) follows the 1-Wire line, so a freshly reset DS248X reads 0x10 or 0x18
    TEST_ASSERT_TRUE(matchesDs248xResetStatus(0x10));
    TEST_ASSERT_TRUE(matchesDs248xResetStatus(0x18));
}

void test_rejects_tdeck_pro_cst328_reply()
{
    TEST_ASSERT_FALSE(matchesDs248xResetStatus(0x2c));
}

void test_rejects_presence_and_short_bits_without_reset()
{
    TEST_ASSERT_FALSE(matchesDs248xResetStatus(0x02));
    TEST_ASSERT_FALSE(matchesDs248xResetStatus(0x04));
    TEST_ASSERT_FALSE(matchesDs248xResetStatus(0x06));
}

void test_rejects_no_reply_and_floating_bus()
{
    TEST_ASSERT_FALSE(matchesDs248xResetStatus(0x00));
    TEST_ASSERT_FALSE(matchesDs248xResetStatus(0xFF));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_accepts_reset_status_with_either_line_level);
    RUN_TEST(test_rejects_tdeck_pro_cst328_reply);
    RUN_TEST(test_rejects_presence_and_short_bits_without_reset);
    RUN_TEST(test_rejects_no_reply_and_floating_bus);
    exit(UNITY_END());
}

void loop() {}
