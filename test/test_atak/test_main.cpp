#include <string.h>
#include <unity.h>

#include "TestUtil.h"
#include "meshUtils.h"

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
    // clean stuff up here
}

/**
 * Test normal string without embedded nulls
 * Should behave the same as strlen() for regular strings
 */
void test_normal_string(void)
{
    char test_str[32] = "Hello World";
    size_t expected = 11; // strlen("Hello World")
    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(expected, result);
}

/**
 * Test empty string
 * Should return 0 for empty string
 */
void test_empty_string(void)
{
    char test_str[32] = "";
    size_t expected = 0;
    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(expected, result);
}

/**
 * Test string with only trailing nulls
 * Common case - string followed by null padding
 */
void test_trailing_nulls(void)
{
    char test_str[32] = {0};
    strcpy(test_str, "Test");
    // test_str is now: "Test\0\0\0\0..." (4 chars + 28 nulls)
    size_t expected = 4;
    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(expected, result);
}

/**
 * Test string with embedded null byte
 * This is the critical bug case - strlen() would truncate at first null
 */
void test_embedded_null(void)
{
    char test_str[32] = {0};
    // Create string "ABC\0XYZ" (embedded null after C)
    test_str[0] = 'A';
    test_str[1] = 'B';
    test_str[2] = 'C';
    test_str[3] = '\0'; // embedded null
    test_str[4] = 'X';
    test_str[5] = 'Y';
    test_str[6] = 'Z';
    // Rest is already null from initialization

    // strlen would return 3, but pb_string_length should return 7
    size_t strlen_result = strlen(test_str);
    size_t pb_result = pb_string_length(test_str, sizeof(test_str));

    TEST_ASSERT_EQUAL_size_t(3, strlen_result); // strlen stops at first null
    TEST_ASSERT_EQUAL_size_t(7, pb_result);     // pb_string_length finds last non-null
}

/**
 * Test Android UID with embedded null bytes
 * Real-world case from bug report: ANDROID-e7e455b40002429d
 * The "00" in the UID represents 0x00 bytes that were truncating the string
 */
void test_android_uid_pattern(void)
{
    char test_str[32] = {0};
    // Simulate "ANDROID-e7e455b4" + 0x00 + 0x00 + "2429d"
    const char part1[] = "ANDROID-e7e455b4";
    strcpy(test_str, part1);
    size_t pos = strlen(part1);
    test_str[pos] = '\0';     // embedded null
    test_str[pos + 1] = '\0'; // another embedded null
    strcpy(test_str + pos + 2, "2429d");

    // The full UID should be 24 characters
    size_t strlen_result = strlen(test_str);
    size_t pb_result = pb_string_length(test_str, sizeof(test_str));

    TEST_ASSERT_EQUAL_size_t(16, strlen_result); // strlen truncates to "ANDROID-e7e455b4"
    TEST_ASSERT_EQUAL_size_t(23, pb_result);     // pb_string_length gets full length
}

/**
 * Test string with multiple embedded nulls
 * Edge case with several null bytes scattered through the string
 */
void test_multiple_embedded_nulls(void)
{
    char test_str[32] = {0};
    // Create "A\0B\0C\0D" (3 embedded nulls)
    test_str[0] = 'A';
    test_str[1] = '\0';
    test_str[2] = 'B';
    test_str[3] = '\0';
    test_str[4] = 'C';
    test_str[5] = '\0';
    test_str[6] = 'D';

    size_t strlen_result = strlen(test_str);
    size_t pb_result = pb_string_length(test_str, sizeof(test_str));

    TEST_ASSERT_EQUAL_size_t(1, strlen_result); // strlen stops at first null
    TEST_ASSERT_EQUAL_size_t(7, pb_result);     // pb_string_length finds all chars
}

/**
 * Test buffer completely filled with non-null characters
 * Edge case where string uses entire buffer
 */
void test_full_buffer(void)
{
    char test_str[8];
    // Fill entire buffer with 'X'
    memset(test_str, 'X', sizeof(test_str));

    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(8, result);
}

/**
 * Test buffer with all nulls
 * Should return 0
 */
void test_all_nulls(void)
{
    char test_str[32] = {0};
    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(0, result);
}

/**
 * Test single character followed by nulls
 * Minimal non-empty case
 */
void test_single_char(void)
{
    char test_str[32] = {0};
    test_str[0] = 'X';

    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(1, result);
}

/**
 * Test callsign field typical size
 * Test with typical ATAK callsign field size (64 bytes)
 */
void test_callsign_field_size(void)
{
    char test_str[64] = {0};
    strcpy(test_str, "CALLSIGN-123");

    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(12, result);
}

/**
 * Test with data at end of buffer
 * String with embedded null and data at very end
 */
void test_data_at_buffer_end(void)
{
    char test_str[10] = {0};
    test_str[0] = 'A';
    test_str[1] = '\0';
    test_str[8] = 'Z'; // Data near end
    test_str[9] = 'X'; // Data at end

    size_t result = pb_string_length(test_str, sizeof(test_str));
    TEST_ASSERT_EQUAL_size_t(10, result); // Should find the 'X' at position 9
}

void setup()
{
    // NOTE!!! Wait for >2 secs
    // if board doesn't support software reset via Serial.DTR/RTS
    testDelay(10);
    testDelay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_normal_string);
    RUN_TEST(test_empty_string);
    RUN_TEST(test_trailing_nulls);
    RUN_TEST(test_embedded_null);
    RUN_TEST(test_android_uid_pattern);
    RUN_TEST(test_multiple_embedded_nulls);
    RUN_TEST(test_full_buffer);
    RUN_TEST(test_all_nulls);
    RUN_TEST(test_single_char);
    RUN_TEST(test_callsign_field_size);
    RUN_TEST(test_data_at_buffer_end);
    exit(UNITY_END());
}

void loop() {}
