#include "meshUtils.h"
#include <cstring>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// --- Valid UTF-8 should pass through unchanged ---

void test_ascii_unchanged()
{
    char buf[32] = "Hello World";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Hello World", buf);
}

void test_valid_2byte_unchanged()
{
    // "café" — é is C3 A9
    char buf[16] = "caf\xC3\xA9";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", buf);
}

void test_valid_3byte_unchanged()
{
    // "€" is E2 82 AC
    char buf[16] = "\xE2\x82\xAC";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("\xE2\x82\xAC", buf);
}

void test_valid_4byte_emoji_unchanged()
{
    // 🌙 is F0 9F 8C 99
    char buf[16] = "\xF0\x9F\x8C\x99";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x8C\x99", buf);
}

void test_valid_mixed_unchanged()
{
    // "Hi 🌙!" — mix of ASCII and 4-byte
    char buf[16] = "Hi \xF0\x9F\x8C\x99!";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Hi \xF0\x9F\x8C\x99!", buf);
}

void test_empty_string()
{
    char buf[8] = "";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// --- Invalid sequences observed in the wild ---

void test_truncated_4byte_at_end()
{
    // Name with valid emoji 🌙 followed by a truncated 4-byte sequence + ASCII
    char buf[32] = "Lunar Tower \xF0\x9F\x8C\x99\xF0\x9F\x97"
                   "4";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    // The 🌙 should be preserved; F0 9F 97 is an incomplete 4-byte sequence,
    // '4' (0x34) is not a valid continuation byte
    TEST_ASSERT_EQUAL_STRING("Lunar Tower \xF0\x9F\x8C\x99???4", buf);
}

void test_lone_lead_bytes_without_continuations()
{
    // Mixed ASCII with stray multibyte lead bytes (E1, F3) lacking proper continuations
    char buf[32] = "Mesht\xE1\xF3tic 37e2";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    // E1 expects 2 continuation bytes, but F3 is not a continuation → E1 replaced
    // F3 expects 3 continuation bytes, 't','i','c' are not continuations → F3 replaced
    TEST_ASSERT_EQUAL_STRING("Mesht??tic 37e2", buf);
}

// --- Edge cases ---

void test_bare_continuation_byte()
{
    // 0x80 alone is invalid (continuation byte with no lead)
    char buf[8] = "\x80";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("?", buf);
}

void test_overlong_2byte()
{
    // C0 AF is an overlong encoding of U+002F '/'
    char buf[8] = "\xC0\xAF";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    // C0 is a 2-byte lead, AF is valid continuation, but codepoint 0x2F < 0x80 → overlong
    // C0 replaced, AF (now bare continuation) also replaced
    TEST_ASSERT_EQUAL_STRING("??", buf);
}

void test_surrogate_half()
{
    // ED A0 80 encodes U+D800 (surrogate half — invalid in UTF-8)
    char buf[8] = "\xED\xA0\x80";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("???", buf);
}

void test_5byte_sequence_rejected()
{
    // F8 80 80 80 80 — 5-byte sequence, not valid UTF-8
    char buf[8] = "\xF8\x80\x80\x80\x80";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    // F8 is invalid lead (>= 0xF8), each 0x80 is bare continuation
    TEST_ASSERT_EQUAL_STRING("?????", buf);
}

void test_truncated_3byte_at_buffer_end()
{
    // Buffer is exactly 4 bytes: E2 82 then forced null at [3]
    char buf[4];
    buf[0] = '\xE2';
    buf[1] = '\x82';
    buf[2] = '\0'; // String ends before the 3-byte sequence completes
    buf[3] = '\0';
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("??", buf);
}

void test_null_termination_enforced()
{
    // Fill buffer completely with no null terminator
    char buf[5];
    memset(buf, 'A', sizeof(buf));
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
    // Should be null-terminated and content preserved (all ASCII)
    TEST_ASSERT_EQUAL_STRING("AAAA", buf);
}

void test_null_buffer()
{
    TEST_ASSERT_FALSE(sanitizeUtf8(nullptr, 10));
}

void test_zero_size()
{
    char buf[4] = "Hi";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, 0));
    // Buffer should be untouched
    TEST_ASSERT_EQUAL_STRING("Hi", buf);
}

void test_valid_max_codepoint()
{
    // U+10FFFF = F4 8F BF BF (maximum valid Unicode codepoint)
    char buf[8] = "\xF4\x8F\xBF\xBF";
    TEST_ASSERT_FALSE(sanitizeUtf8(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("\xF4\x8F\xBF\xBF", buf);
}

void test_above_max_codepoint()
{
    // U+110000 = F4 90 80 80 (just above maximum valid Unicode)
    char buf[8] = "\xF4\x90\x80\x80";
    TEST_ASSERT_TRUE(sanitizeUtf8(buf, sizeof(buf)));
}

void setup()
{
    UNITY_BEGIN();

    // Valid UTF-8 passthrough
    RUN_TEST(test_ascii_unchanged);
    RUN_TEST(test_valid_2byte_unchanged);
    RUN_TEST(test_valid_3byte_unchanged);
    RUN_TEST(test_valid_4byte_emoji_unchanged);
    RUN_TEST(test_valid_mixed_unchanged);
    RUN_TEST(test_empty_string);

    // Invalid sequences observed in the wild
    RUN_TEST(test_truncated_4byte_at_end);
    RUN_TEST(test_lone_lead_bytes_without_continuations);

    // Edge cases
    RUN_TEST(test_bare_continuation_byte);
    RUN_TEST(test_overlong_2byte);
    RUN_TEST(test_surrogate_half);
    RUN_TEST(test_5byte_sequence_rejected);
    RUN_TEST(test_truncated_3byte_at_buffer_end);
    RUN_TEST(test_null_termination_enforced);
    RUN_TEST(test_null_buffer);
    RUN_TEST(test_zero_size);
    RUN_TEST(test_valid_max_codepoint);
    RUN_TEST(test_above_max_codepoint);

    exit(UNITY_END());
}

void loop() {}
