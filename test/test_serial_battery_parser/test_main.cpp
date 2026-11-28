// Unit tests for SerialBatteryParser in src/power/SerialBatteryParser.cpp - the decoder for the battery
// reports the ELECROW ThinkNode M4's power-bank MCU sends over a 4800 baud software UART.
//
// A frame is FE, the charge percent, three base-100 digit pairs giving half the pack voltage (volts,
// hundredths, ten-thousandths), and FD. There is no checksum, so the end byte is the only check.
// SerialBatteryPowerBank reports the last decoded frame as the node's battery level and voltage. These
// tests pin the layout the M4 used before it moved to the PowerBank interface, so the move changes
// nothing a client sees.
//
// Regressions guarded:
//  - the voltage pairs read in the wrong order or without the factor of two, which halves or scrambles
//    every reported voltage and can trip the low-voltage shutdown;
//  - a frame without its end byte overwriting the last good report;
//  - a start byte in the end position dropped instead of starting the next frame, which loses a frame
//    after every glitch;
//  - a charge level above 100 passed through, which reads as a negative percent once narrowed.
#include "Arduino.h" // declares setup()/loop() with the linkage the framework's main() calls
#include "TestUtil.h"
#include "power/SerialBatteryParser.h"
#include <cstdlib>
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

// Feeds bytes and returns how many of them completed a valid frame.
static int feedAll(SerialBatteryParser &parser, const uint8_t *bytes, size_t len)
{
    int frames = 0;
    for (size_t i = 0; i < len; i++)
        frames += parser.feed(bytes[i]) ? 1 : 0;
    return frames;
}

// 87 %, half the pack at 1.9925 V, so 3985 mV.
static const uint8_t kFrame[] = {0xFE, 87, 1, 99, 25, 0xFD};

void setUp(void) {}
void tearDown(void) {}

void test_no_status_before_the_first_frame(void)
{
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL_UINT32(0, parser.goodFrames());
}

void test_valid_frame_decodes_percent_and_voltage(void)
{
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, kFrame, sizeof(kFrame)));
    TEST_ASSERT_EQUAL_UINT8(87, parser.status().batteryPercent);
    TEST_ASSERT_EQUAL_UINT16(3985, parser.status().batteryMv);
}

// The pairs are not interchangeable: 4.0050 V half voltage is 8010 mV, 4.5000 would be 9000.
void test_voltage_pairs_are_volts_then_hundredths_then_ten_thousandths(void)
{
    const uint8_t frame[] = {0xFE, 50, 4, 0, 50, 0xFD};
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT16(8010, parser.status().batteryMv);
}

void test_battery_percent_is_capped_at_100(void)
{
    const uint8_t frame[] = {0xFE, 180, 2, 0, 0, 0xFD};
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(100, parser.status().batteryPercent);
}

void test_missing_end_byte_is_rejected_and_keeps_the_last_status(void)
{
    const uint8_t bad[] = {0xFE, 10, 1, 50, 0, 0x00};
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, kFrame, sizeof(kFrame)));
    TEST_ASSERT_EQUAL(0, feedAll(parser, bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_UINT8(87, parser.status().batteryPercent);
    TEST_ASSERT_EQUAL_UINT16(3985, parser.status().batteryMv);
    TEST_ASSERT_EQUAL_UINT32(1, parser.badFrames());
}

// A start byte where the end byte should be is the start of the next frame, not more noise.
void test_start_byte_in_the_end_position_starts_the_next_frame(void)
{
    const uint8_t stream[] = {0xFE, 10, 1, 50, 0, 0xFE, 87, 1, 99, 25, 0xFD};
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, stream, sizeof(stream)));
    TEST_ASSERT_EQUAL_UINT8(87, parser.status().batteryPercent);
    TEST_ASSERT_EQUAL_UINT32(1, parser.badFrames());
}

void test_noise_before_a_frame_is_skipped(void)
{
    const uint8_t stream[] = {0x00, 0xFD, 0x57, 0xFE, 87, 1, 99, 25, 0xFD};
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, stream, sizeof(stream)));
    TEST_ASSERT_EQUAL_UINT16(3985, parser.status().batteryMv);
}

void test_back_to_back_frames_each_decode(void)
{
    const uint8_t second[] = {0xFE, 42, 1, 80, 0, 0xFD};
    SerialBatteryParser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, kFrame, sizeof(kFrame)));
    TEST_ASSERT_EQUAL(1, feedAll(parser, second, sizeof(second)));
    TEST_ASSERT_EQUAL_UINT8(42, parser.status().batteryPercent);
    TEST_ASSERT_EQUAL_UINT16(3600, parser.status().batteryMv);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_no_status_before_the_first_frame);
    RUN_TEST(test_valid_frame_decodes_percent_and_voltage);
    RUN_TEST(test_voltage_pairs_are_volts_then_hundredths_then_ten_thousandths);
    RUN_TEST(test_battery_percent_is_capped_at_100);
    RUN_TEST(test_missing_end_byte_is_rejected_and_keeps_the_last_status);
    RUN_TEST(test_start_byte_in_the_end_position_starts_the_next_frame);
    RUN_TEST(test_noise_before_a_frame_is_skipped);
    RUN_TEST(test_back_to_back_frames_each_decode);
    exit(UNITY_END());
}

void loop() {}
