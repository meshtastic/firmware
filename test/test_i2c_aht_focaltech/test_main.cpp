// Unit tests for isFocalTechTouchId() in src/detect/ScanI2CTwoWire.cpp - the 0x38 AHT vs. FocalTech probe.
//
// I2C address 0x38 is shared between AHT10/AHT20/AHT21 temperature/humidity sensors and FocalTech touch
// controllers (FT6x06/FT6x36/FT6336U/FT5x06). Commit afdaccc25f5 added a disambiguation probe reading
// FocalTech registers 0xA8 (vendor) and 0xA3 (chip) under the assumption that an AHT has no register map
// and always answers with the exact same status byte.
//
// In practice, 0xA8 is an AHT command byte (NOR/measurement-related). Measured on a Station G2 with an
// AHT20+BMP280 board, reading 0xA8 returned 0x18 (idle + calibrated) and immediately reading 0xA3 returned
// 0x98 (0x18 | 0x80, bit 7 BUSY set). Because 0x98 != 0x18, ScanI2CTwoWire logged:
// "FocalTech touch (chip 0x98, vendor 0x18) at 0x38, not an AHT10" and skipped initializing the AHT20.
//
// Guarding against 0x00/0xFF bus reads and masking out bit 7 (BUSY) preserves FocalTech touch detection
// while preventing AHT status transitions from being misclassified as a touch controller.
#include "Arduino.h"
#include "TestUtil.h"
#include "detect/ScanI2CTwoWire.h"
#include <unity.h>

void test_aht20_busy_bit_flip_is_not_focaltech(void)
{
    // Exact regression measured on hardware: AHT20 returns 0x18 (idle/calibrated) then 0x98 (busy/calibrated).
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x18, 0x98));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x98, 0x18));
}

void test_aht_identical_or_busy_status_bytes_are_not_focaltech(void)
{
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x18, 0x18));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x08, 0x08));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x08, 0x88));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x00, 0x80));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x1C, 0x9C));
}

void test_zero_or_ff_bus_reads_are_not_focaltech(void)
{
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x00, 0x36));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x11, 0x00));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0xFF, 0x36));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x11, 0xFF));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0x00, 0x00));
    TEST_ASSERT_FALSE(isFocalTechTouchId(0xFF, 0xFF));
}

void test_focaltech_controllers_are_detected(void)
{
    // FT6206 (vendor=0x11, chip=0x06)
    TEST_ASSERT_TRUE(isFocalTechTouchId(0x11, 0x06));
    // FT6236 / Makerfabs Nomad Terminal (vendor=0x11, chip=0x36)
    TEST_ASSERT_TRUE(isFocalTechTouchId(0x11, 0x36));
    // FT6336U (vendor=0x11, chip=0x64)
    TEST_ASSERT_TRUE(isFocalTechTouchId(0x11, 0x64));
    // FT5x06 (vendor=0x11, chip=0x55)
    TEST_ASSERT_TRUE(isFocalTechTouchId(0x11, 0x55));
    // OEM CTPM panels
    TEST_ASSERT_TRUE(isFocalTechTouchId(0x79, 0x54));
    TEST_ASSERT_TRUE(isFocalTechTouchId(0x51, 0x36));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_aht20_busy_bit_flip_is_not_focaltech);
    RUN_TEST(test_aht_identical_or_busy_status_bytes_are_not_focaltech);
    RUN_TEST(test_zero_or_ff_bus_reads_are_not_focaltech);
    RUN_TEST(test_focaltech_controllers_are_detected);
    exit(UNITY_END());
}

void loop() {}
