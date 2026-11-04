// Unit tests for qrEncodeText() / qrGetModule() in src/graphics/QrEncode.cpp - the encoder behind the
// BLE pairing QR frame (DebugRenderer::drawFrameBluetoothPair).
//
// The frame encodes the app's connect link, meshtastic://meshtastic/connections?address=x<MAC>&pin=<PIN>,
// which a phone camera must read back byte for byte or the app opens on the wrong device or PIN. The
// encoder is a cut-down ricmoo/QRCode fixed at ECC level M, versions 1-6 and byte mode, so a symbol is
// only correct if every module matches what a full encoder produces for the same version and mask.
//
// Regressions guarded:
// - any change to placement, interleaving, Reed-Solomon, masking or format bits that moves a module.
//   The reference matrix below is Project Nayuki's encoder output for the 73-character link (version 5,
//   mask 2, which both encoders choose), and it decodes with zxing;
// - picking a larger version than needed (a coarser code on 128x64 panels) or a smaller one that the
//   data overflows, at each version's byte-mode capacity boundary;
// - writing past the module buffer instead of rejecting data longer than version 6 holds.
#include "TestUtil.h"
#include "graphics/QrEncode.h"
#include <Arduino.h>
#include <cstring>
#include <string>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static const char *const kLink = "meshtastic://meshtastic/connections?address=xAC:27:6E:CD:5A:99&pin=317338";

static const char *const kLinkModules[] = {
    "1111111000101001111011111001101111111", "1000001000101101010100000101101000001", "1011101010100110101111100011101011101",
    "1011101011000100110100111111101011101", "1011101011001001000111010000101011101", "1000001011100010001010000001101000001",
    "1111111010101010101010101010101111111", "0000000010001000100111110011100000000", "1011111001000011001110010101101111100",
    "0011010001110101100000111110010001110", "1000111101001010011010101001000011011", "1111000110101110100001110000011010011",
    "0000101001100100111000111110011011011", "1001010101100010110111011110100101010", "1010111010100111110011000101111000111",
    "0101000011011001111001111001100100011", "1101101011001111100100001100111111110", "1010100110001101011010110010001001000",
    "1110001001011101110100100011001110011", "0111010000110111000111001001110100011", "0010011011010111010100111111001111010",
    "0100110101101110101111010100110101110", "0011101000101001001000100001111111001", "0111010000111100000011101010111000010",
    "0101001101111101001000011100001110101", "1110010110110111100000110011110001000", "1011001100000100001000101001100010111",
    "1000110011111100100001001010000001011", "1010001010011010110110101111111110100", "0000000010001100111111010111100010010",
    "1111111000001111010010001000101011011", "1000001010101101111101100011100010010", "1011101011001001100010000101111111101",
    "1011101011000001001101110000101111101", "1011101011001011100011101100100101111", "1000001000000111001101110011000101001",
    "1111111011011101010000100110000000111"};

static uint8_t modules[QR_ENCODE_BUFFER_SIZE(QR_ENCODE_MAX_VERSION)];

static void test_connect_link_matches_reference_symbol()
{
    QrEncode qr;
    TEST_ASSERT_TRUE(qrEncodeText(&qr, modules, kLink));
    TEST_ASSERT_EQUAL_UINT8(5, qr.version);
    TEST_ASSERT_EQUAL_UINT8(37, qr.size);
    TEST_ASSERT_EQUAL_UINT8(2, qr.mask);
    for (uint8_t y = 0; y < qr.size; y++) {
        char row[38];
        for (uint8_t x = 0; x < qr.size; x++)
            row[x] = qrGetModule(&qr, x, y) ? '1' : '0';
        row[qr.size] = '\0';
        TEST_ASSERT_EQUAL_STRING_MESSAGE(kLinkModules[y], row, "module row differs from reference");
    }
}

// Byte-mode capacity at ECC M: 14, 26, 42, 62, 84, 106 characters for versions 1-6.
static void test_smallest_version_is_chosen_at_each_capacity_boundary()
{
    static const uint16_t capacity[6] = {14, 26, 42, 62, 84, 106};
    for (uint8_t v = 1; v <= 6; v++) {
        QrEncode qr;
        std::string fits(capacity[v - 1], 'a');
        TEST_ASSERT_TRUE(qrEncodeText(&qr, modules, fits.c_str()));
        TEST_ASSERT_EQUAL_UINT8(v, qr.version);
        TEST_ASSERT_EQUAL_UINT8(4 * v + 17, qr.size);
        if (v < 6) {
            std::string over(capacity[v - 1] + 1, 'a');
            TEST_ASSERT_TRUE(qrEncodeText(&qr, modules, over.c_str()));
            TEST_ASSERT_EQUAL_UINT8(v + 1, qr.version);
        }
    }
}

static void test_data_beyond_version_6_is_rejected()
{
    QrEncode qr;
    std::string tooLong(107, 'a');
    TEST_ASSERT_FALSE(qrEncodeText(&qr, modules, tooLong.c_str()));
}

static void test_getModule_outside_symbol_is_light()
{
    QrEncode qr;
    TEST_ASSERT_TRUE(qrEncodeText(&qr, modules, "x"));
    TEST_ASSERT_TRUE(qrGetModule(&qr, 0, 0)); // finder corner
    TEST_ASSERT_FALSE(qrGetModule(&qr, qr.size, 0));
    TEST_ASSERT_FALSE(qrGetModule(&qr, 0, qr.size));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_connect_link_matches_reference_symbol);
    RUN_TEST(test_smallest_version_is_chosen_at_each_capacity_boundary);
    RUN_TEST(test_data_beyond_version_6_is_rejected);
    RUN_TEST(test_getModule_outside_symbol_is_light);
    exit(UNITY_END());
}

void loop() {}
