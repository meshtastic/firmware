// Unit tests for MAC_from_string in src/platform/portduino/PortduinoGlue.cpp.
//
// Regression coverage for the where the function stripped colons from
// its mac_str parameter but then read bytes from the global
// portduino_config.mac_address. Symptoms: --hwid silently ignored when
// MACAddress: was also set, and SIGABRT (stoi: no conversion) when --hwid
// was used without MACAddress: in config.yaml.
#include "Arduino.h"
#include "TestUtil.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <unity.h>

// Forward-declare instead of including PortduinoGlue.h to avoid pulling in
// LR11x0Interface, USBHal, mesh.pb.h, yaml-cpp, and the full portduino_config
// struct just to test a self-contained string parser. The symbol is defined
// in PortduinoGlue.cpp and resolved at link time.
bool MAC_from_string(std::string mac_str, uint8_t *dmac);

void setUp(void) {}
void tearDown(void) {}

// --- Happy-path parsing ---

void test_colon_separated_uppercase()
{
    uint8_t dmac[6] = {0};
    TEST_ASSERT_TRUE(MAC_from_string("AA:BB:CC:DD:EE:FF", dmac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, dmac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, dmac[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, dmac[2]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, dmac[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, dmac[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, dmac[5]);
}

void test_colon_separated_lowercase()
{
    uint8_t dmac[6] = {0};
    TEST_ASSERT_TRUE(MAC_from_string("02:ca:fe:ba:be:01", dmac));
    TEST_ASSERT_EQUAL_HEX8(0x02, dmac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCA, dmac[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, dmac[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBA, dmac[3]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, dmac[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, dmac[5]);
}

void test_no_colons_packed_hex()
{
    // The CLI form produced by some tools — 12 hex chars, no separators.
    uint8_t dmac[6] = {0};
    TEST_ASSERT_TRUE(MAC_from_string("AABBCCDDEEFF", dmac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, dmac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, dmac[5]);
}

void test_two_distinct_inputs_yield_distinct_outputs()
{
    // Direct regression for the original bug: parsing two different MAC
    // strings in succession must produce two different byte sequences.
    // Pre-fix, both calls would have produced identical bytes derived from
    // the (untouched) global portduino_config.mac_address.
    uint8_t a[6] = {0};
    uint8_t b[6] = {0};
    TEST_ASSERT_TRUE(MAC_from_string("AA:BB:CC:DD:EE:FF", a));
    TEST_ASSERT_TRUE(MAC_from_string("02:CA:FE:BA:BE:01", b));
    TEST_ASSERT_NOT_EQUAL(0, std::memcmp(a, b, 6));
    TEST_ASSERT_EQUAL_HEX8(0xAA, a[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, b[0]);
}

void test_does_not_read_external_state()
{
    // The function must derive every byte from its parameter, not from any
    // global. Provide a unique MAC and verify all six bytes match the input
    // exactly — leaves no room for the function to be smuggling bytes from
    // elsewhere.
    uint8_t dmac[6] = {0};
    TEST_ASSERT_TRUE(MAC_from_string("12:34:56:78:9A:BC", dmac));
    const uint8_t expected[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, dmac, 6);
}

// --- Rejected inputs ---
// Pre-fix, the empty/short cases either crashed (stoi exception on substr("")
// of the empty global) or silently filled dmac with stale bytes. Post-fix,
// the length guard rejects them cleanly with `false` and dmac is unchanged.

void test_empty_string_returns_false()
{
    uint8_t dmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11};
    uint8_t before[6];
    std::memcpy(before, dmac, 6);
    TEST_ASSERT_FALSE(MAC_from_string("", dmac));
    // dmac must be untouched on failure.
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, dmac, 6);
}

void test_too_short_returns_false()
{
    uint8_t dmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11};
    uint8_t before[6];
    std::memcpy(before, dmac, 6);
    TEST_ASSERT_FALSE(MAC_from_string("AA:BB:CC", dmac));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, dmac, 6);
}

void test_too_long_returns_false()
{
    uint8_t dmac[6] = {0};
    // 14 hex chars after colon-strip > 12.
    TEST_ASSERT_FALSE(MAC_from_string("AA:BB:CC:DD:EE:FF:00", dmac));
}

void test_only_colons_returns_false()
{
    uint8_t dmac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11};
    uint8_t before[6];
    std::memcpy(before, dmac, 6);
    TEST_ASSERT_FALSE(MAC_from_string(":::::", dmac));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, dmac, 6);
}

void test_extra_colons_still_parses()
{
    // Colon stripping happens before length check, so an unconventional
    // grouping that totals 12 hex chars after stripping is still accepted.
    uint8_t dmac[6] = {0};
    TEST_ASSERT_TRUE(MAC_from_string("AABB:CCDD:EEFF", dmac));
    TEST_ASSERT_EQUAL_HEX8(0xAA, dmac[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, dmac[5]);
}

// --- Unity lifecycle ---

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_colon_separated_uppercase);
    RUN_TEST(test_colon_separated_lowercase);
    RUN_TEST(test_no_colons_packed_hex);
    RUN_TEST(test_two_distinct_inputs_yield_distinct_outputs);
    RUN_TEST(test_does_not_read_external_state);
    RUN_TEST(test_empty_string_returns_false);
    RUN_TEST(test_too_short_returns_false);
    RUN_TEST(test_too_long_returns_false);
    RUN_TEST(test_only_colons_returns_false);
    RUN_TEST(test_extra_colons_still_parses);
    exit(UNITY_END());
}

void loop() {}
