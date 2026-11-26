// Unit tests for HLT331Parser in src/power/HLT331Parser.cpp - the decoder for the status frames the
// Heltec Mesh Pocket V2's HLT331 power-bank controller streams on its UART.
//
// A frame is A5 5A, a length byte (13), 13 data bytes and a CRC-8/MAXIM over the length and the data.
// HLT331PowerBank feeds every received byte through feed() and reports the last decoded status as the
// node's battery level, battery voltage, external power and charging state, so a misread field is a
// wrong battery reading in every client, and a frame accepted with a bad CRC is a random one. The
// byte layout was read from Heltec's driver; these tests pin it, so a change to it is a decision
// rather than an accident.
//
// Regressions guarded:
//  - a CRC that leaves out the length byte, or uses another polynomial, rejects every real frame and
//    the board reports no battery at all;
//  - a frame with a bad CRC or length overwriting the last good status;
//  - losing sync after line noise, or after a repeated A5, so later good frames are dropped;
//  - swapped or shifted fields: C1 and C2 states, the 7-bit charge level, the little-endian voltages,
//    the 10 mW unit of the wireless power, and the signed temperatures;
//  - external power read only from the charging bit, so a full bank on a charger reads as unplugged.
#include "Arduino.h" // declares setup()/loop() with the linkage the framework's main() calls
#include "TestUtil.h"
#include "power/HLT331Parser.h"
#include <cstdlib>
#include <stdint.h>
#include <string.h>
#include <unity.h>

static constexpr size_t kFrameLen = 2 + 1 + HLT331Parser::DATA_LENGTH + 1;

// Builds a complete frame around data, with the CRC computed over the length byte and the data.
static void buildFrame(const uint8_t data[HLT331Parser::DATA_LENGTH], uint8_t frame[kFrameLen])
{
    frame[0] = 0xA5;
    frame[1] = 0x5A;
    frame[2] = HLT331Parser::DATA_LENGTH;
    memcpy(&frame[3], data, HLT331Parser::DATA_LENGTH);
    frame[kFrameLen - 1] = HLT331Parser::crc8(&frame[2], 1 + HLT331Parser::DATA_LENGTH);
}

// Feeds bytes and returns how many of them completed a valid frame.
static int feedAll(HLT331Parser &parser, const uint8_t *bytes, size_t len)
{
    int frames = 0;
    for (size_t i = 0; i < len; i++)
        frames += parser.feed(bytes[i]) ? 1 : 0;
    return frames;
}

// Every field set to a value that would read differently if its bits or bytes were misplaced.
// clang-format off
static const uint8_t kData[HLT331Parser::DATA_LENGTH] = {
    (87 << 1) | 1,                    // 87 %, charging
    0x01 | (0x02 << 2) | 0x10 | 0x40, // C1 charging, C2 discharging, NTC over-temperature, foreign object
    0x06 | (0x07 << 4),               // wireless protocol 6, fast-charge mode 7
    0xF6,                             // NTC1 -10 C
    0x9A, 0x0F,                       // battery 3994 mV
    0x8C, 0x23,                       // bus 9100 mV
    0xE8, 0x03,                       // bus 1000 mA
    0xF4, 0x01,                       // wireless 500 x 10 mW
    0x2D,                             // NTC2 45 C
};
// clang-format on

void setUp(void) {}
void tearDown(void) {}

// The standard check value of CRC-8/MAXIM (reflected 0x31, init 0) over "123456789".
void test_crc8_matches_the_maxim_check_value(void)
{
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xA1, HLT331Parser::crc8(check, sizeof(check)));
}

void test_valid_frame_decodes_every_field(void)
{
    uint8_t frame[kFrameLen];
    buildFrame(kData, frame);
    HLT331Parser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, frame, sizeof(frame)));

    const HLT331Status &s = parser.status();
    TEST_ASSERT_TRUE(s.charging);
    TEST_ASSERT_EQUAL_UINT8(87, s.batteryPercent);
    TEST_ASSERT_EQUAL_UINT8(HLT331Status::PORT_CHARGING, s.c1State);
    TEST_ASSERT_EQUAL_UINT8(HLT331Status::PORT_DISCHARGING, s.c2State);
    TEST_ASSERT_TRUE(s.ntcOverTemp);
    TEST_ASSERT_FALSE(s.wirelessActive);
    TEST_ASSERT_TRUE(s.wirelessForeignObject);
    TEST_ASSERT_FALSE(s.wirelessFull);
    TEST_ASSERT_EQUAL_UINT8(6, s.wirelessProtocol);
    TEST_ASSERT_EQUAL_UINT8(7, s.fastChargeMode);
    TEST_ASSERT_EQUAL_INT8(-10, s.ntc1TempC);
    TEST_ASSERT_EQUAL_UINT16(3994, s.batteryMv);
    TEST_ASSERT_EQUAL_UINT16(9100, s.busMv);
    TEST_ASSERT_EQUAL_UINT16(1000, s.busMa);
    TEST_ASSERT_EQUAL_UINT32(5000, s.wirelessMw);
    TEST_ASSERT_EQUAL_INT8(45, s.ntc2TempC);
    TEST_ASSERT_EQUAL_UINT32(1, parser.goodFrames());
}

// The charge level has 7 bits; anything past 100 is clamped rather than reported as more than full.
void test_battery_percent_is_capped_at_100(void)
{
    uint8_t data[HLT331Parser::DATA_LENGTH];
    memcpy(data, kData, sizeof(data));
    data[0] = 0xFE; // 127 %, not charging
    uint8_t frame[kFrameLen];
    buildFrame(data, frame);
    HLT331Parser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(100, parser.status().batteryPercent);
    TEST_ASSERT_FALSE(parser.status().charging);
}

void test_bad_crc_is_rejected_and_keeps_the_last_status(void)
{
    uint8_t good[kFrameLen];
    buildFrame(kData, good);
    uint8_t data[HLT331Parser::DATA_LENGTH];
    memcpy(data, kData, sizeof(data));
    data[4] = 0x00; // battery would read 3840 mV
    uint8_t bad[kFrameLen];
    buildFrame(data, bad);
    bad[kFrameLen - 1] ^= 0x01;

    HLT331Parser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, good, sizeof(good)));
    TEST_ASSERT_EQUAL(0, feedAll(parser, bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_UINT16(3994, parser.status().batteryMv);
    TEST_ASSERT_EQUAL_UINT32(1, parser.badCrcFrames());
    TEST_ASSERT_EQUAL_UINT32(1, parser.goodFrames());
}

// A frame whose CRC was computed over the data alone, without the length byte, is not a valid frame.
void test_crc_covers_the_length_byte(void)
{
    uint8_t frame[kFrameLen];
    buildFrame(kData, frame);
    frame[kFrameLen - 1] = HLT331Parser::crc8(kData, HLT331Parser::DATA_LENGTH);
    HLT331Parser parser;
    TEST_ASSERT_EQUAL(0, feedAll(parser, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT32(1, parser.badCrcFrames());
}

void test_wrong_length_is_rejected(void)
{
    uint8_t frame[kFrameLen];
    buildFrame(kData, frame);
    frame[2] = 12;
    HLT331Parser parser;
    TEST_ASSERT_EQUAL(0, feedAll(parser, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT32(1, parser.badLengthFrames());
    TEST_ASSERT_EQUAL_UINT32(0, parser.goodFrames());
}

// Noise, a lone A5, and A5 A5 5A in front of a frame must not cost the frame itself.
void test_resyncs_after_noise_and_a_repeated_header_byte(void)
{
    uint8_t frame[kFrameLen];
    buildFrame(kData, frame);
    uint8_t stream[6 + kFrameLen];
    const uint8_t noise[] = {0x00, 0x5A, 0xA5, 0x13, 0xFF, 0xA5};
    memcpy(stream, noise, sizeof(noise));
    memcpy(stream + sizeof(noise), frame, sizeof(frame));

    HLT331Parser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, stream, sizeof(stream)));
    TEST_ASSERT_EQUAL_UINT16(3994, parser.status().batteryMv);
}

// A rejected frame leaves the parser ready for the very next one.
void test_frame_after_a_rejected_frame_decodes(void)
{
    uint8_t bad[kFrameLen];
    buildFrame(kData, bad);
    bad[kFrameLen - 1] ^= 0xFF;
    uint8_t good[kFrameLen];
    buildFrame(kData, good);

    HLT331Parser parser;
    TEST_ASSERT_EQUAL(0, feedAll(parser, bad, sizeof(bad)));
    TEST_ASSERT_EQUAL(1, feedAll(parser, good, sizeof(good)));
}

void test_back_to_back_frames_each_decode(void)
{
    uint8_t data[HLT331Parser::DATA_LENGTH];
    memcpy(data, kData, sizeof(data));
    uint8_t first[kFrameLen];
    buildFrame(data, first);
    data[0] = 50 << 1;
    uint8_t second[kFrameLen];
    buildFrame(data, second);

    HLT331Parser parser;
    TEST_ASSERT_EQUAL(1, feedAll(parser, first, sizeof(first)));
    TEST_ASSERT_EQUAL(1, feedAll(parser, second, sizeof(second)));
    TEST_ASSERT_EQUAL_UINT8(50, parser.status().batteryPercent);
    TEST_ASSERT_FALSE(parser.status().charging);
}

// A charger on either port is external power even when the bank is full and the charging bit is clear;
// a port the bank is discharging into is not.
void test_input_present_follows_ports_charging_the_bank(void)
{
    HLT331Status s;
    TEST_ASSERT_FALSE(s.inputPresent());
    s.charging = true;
    TEST_ASSERT_TRUE(s.inputPresent());
    s.charging = false;
    s.c1State = HLT331Status::PORT_CHARGING;
    TEST_ASSERT_TRUE(s.inputPresent());
    s.c1State = HLT331Status::PORT_DISCHARGING;
    TEST_ASSERT_FALSE(s.inputPresent());
    s.c2State = HLT331Status::PORT_CHARGING;
    TEST_ASSERT_TRUE(s.inputPresent());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_crc8_matches_the_maxim_check_value);
    RUN_TEST(test_valid_frame_decodes_every_field);
    RUN_TEST(test_battery_percent_is_capped_at_100);
    RUN_TEST(test_bad_crc_is_rejected_and_keeps_the_last_status);
    RUN_TEST(test_crc_covers_the_length_byte);
    RUN_TEST(test_wrong_length_is_rejected);
    RUN_TEST(test_resyncs_after_noise_and_a_repeated_header_byte);
    RUN_TEST(test_frame_after_a_rejected_frame_decodes);
    RUN_TEST(test_back_to_back_frames_each_decode);
    RUN_TEST(test_input_present_follows_ports_charging_the_bank);
    exit(UNITY_END());
}

void loop() {}
