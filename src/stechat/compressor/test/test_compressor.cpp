/**
 * @file test_compressor.cpp
 * @brief Unit tests for the stechat compressor module
 *
 * Tests cover:
 * 1. Unishox2 bounded API safety
 * 2. Compression/decompression roundtrip
 * 3. MessageBuffer key-by-key input
 * 4. Line management with Enter key
 * 5. 200-byte threshold compression
 * 6. Timeout handling
 * 7. Backspace handling
 *
 * Compile with:
 * cd src/stechat/compressor
 * gcc -c -DUNISHOX_API_WITH_OUTPUT_LEN=1 -Ilib -o lib/unishox2.o lib/unishox2.c
 * g++ -std=c++14 -DUNISHOX_API_WITH_OUTPUT_LEN=1 -DNDEBUG -Iinclude -Ilib \
 *     -o test/test_compressor test/test_compressor.cpp src/Compressor.cpp \
 *     src/MessageBuffer.cpp lib/unishox2.o
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Compressor.h"
#include "MessageBuffer.h"

// Test framework macros
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("  [PASS] %s\n", message); \
    } else { \
        tests_failed++; \
        printf("  [FAIL] %s (line %d)\n", message, __LINE__); \
    } \
} while(0)

#define TEST_SECTION(name) printf("\n=== %s ===\n", name)

// Test state for packet callback
static uint8_t lastPacketData[256];
static size_t lastPacketLen = 0;
static uint16_t lastBatchId = 0;
static uint8_t lastPacketNum = 0;
static bool lastIsFinal = false;
static int packetCallbackCount = 0;
static uint8_t lastPacketFlags = 0;

void resetTestState() {
    memset(lastPacketData, 0, sizeof(lastPacketData));
    lastPacketLen = 0;
    lastBatchId = 0;
    lastPacketNum = 0;
    lastIsFinal = false;
    packetCallbackCount = 0;
    lastPacketFlags = 0;
}

void testPacketCallback(const uint8_t* data, size_t len,
                        uint16_t batchId, uint8_t packetNum, bool isFinal) {
    if (len <= sizeof(lastPacketData)) {
        memcpy(lastPacketData, data, len);
        lastPacketLen = len;
    }
    lastBatchId = batchId;
    lastPacketNum = packetNum;
    lastIsFinal = isFinal;
    lastPacketFlags = (len >= 7) ? data[6] : 0;  // Flags are at byte 6
    packetCallbackCount++;
}

// ============================================================================
// Test: Unishox2 Basic Compression
// ============================================================================
void test_unishox2_basic_compression() {
    TEST_SECTION("Unishox2 Basic Compression");

    stechat::Unishox2 compressor;

    // Test 1: Simple string compression
    const char* input = "Hello World";
    uint8_t output[64];
    size_t compressedLen = compressor.compress(input, output, sizeof(output));

    TEST_ASSERT(compressedLen > 0, "Compression returns non-zero length");
    TEST_ASSERT(compressedLen < strlen(input), "Compressed size is smaller than input");

    // Test 2: Decompress and verify
    char decompressed[64];
    size_t decompressedLen = compressor.decompress(output, compressedLen,
                                                    decompressed, sizeof(decompressed));

    TEST_ASSERT(decompressedLen == strlen(input), "Decompressed length matches original");
    TEST_ASSERT(strcmp(decompressed, input) == 0, "Decompressed content matches original");
}

// ============================================================================
// Test: Unishox2 Bounded API Safety
// ============================================================================
void test_unishox2_bounded_api() {
    TEST_SECTION("Unishox2 Bounded API Safety");

    stechat::Unishox2 compressor;

    // Test 1: Small output buffer should not overflow
    const char* input = "This is a test string that might expand";
    uint8_t tinyOutput[4];  // Very small buffer

    size_t result = compressor.compress(input, tinyOutput, sizeof(tinyOutput));
    // Should return 0 (too small) or a bounded result
    TEST_ASSERT(result <= sizeof(tinyOutput), "Compression respects small output buffer");

    // Test 2: NULL input handling
    result = compressor.compress(nullptr, tinyOutput, sizeof(tinyOutput));
    TEST_ASSERT(result == 0, "NULL input returns 0");

    // Test 3: NULL output handling
    result = compressor.compress(input, nullptr, 64);
    TEST_ASSERT(result == 0, "NULL output returns 0");

    // Test 4: Zero-length output buffer
    uint8_t output[64];
    result = compressor.compress(input, output, 0);
    TEST_ASSERT(result == 0, "Zero-length output buffer returns 0");

    // Test 5: Minimum output length check
    result = compressor.compress(input, output, 2);  // Below MIN_OUTPUT_LEN
    TEST_ASSERT(result == 0, "Output buffer below minimum returns 0");
}

// ============================================================================
// Test: Unishox2 Roundtrip Various Strings
// ============================================================================
void test_unishox2_roundtrip() {
    TEST_SECTION("Unishox2 Compression Roundtrip");

    stechat::Unishox2 compressor;

    const char* testStrings[] = {
        "a",
        "hello",
        "Hello World!",
        "The quick brown fox jumps over the lazy dog",
        "https://www.example.com/path?query=value",
        "user@example.com",
        "12345",
        "!@#$%^&*()",
        "Mixed 123 Content!",
        "   spaces   ",
    };

    for (size_t i = 0; i < sizeof(testStrings) / sizeof(testStrings[0]); i++) {
        const char* input = testStrings[i];
        uint8_t compressed[256];
        char decompressed[256];

        size_t compLen = compressor.compress(input, compressed, sizeof(compressed));

        if (compLen > 0) {
            size_t decompLen = compressor.decompress(compressed, compLen,
                                                      decompressed, sizeof(decompressed));

            char msg[128];
            snprintf(msg, sizeof(msg), "Roundtrip for: '%.30s%s'",
                     input, strlen(input) > 30 ? "..." : "");
            TEST_ASSERT(decompLen == strlen(input) && strcmp(decompressed, input) == 0, msg);
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "Compression of: '%.30s'", input);
            TEST_ASSERT(false, msg);
        }
    }
}

// ============================================================================
// Test: MessageBuffer Begin/Active State
// ============================================================================
void test_messagebuffer_begin() {
    TEST_SECTION("MessageBuffer Begin/Active State");

    stechat::MessageBuffer buffer;

    TEST_ASSERT(!buffer.isActive(), "Buffer starts inactive");

    buffer.begin(1704067200);  // 2024-01-01 00:00:00 UTC

    TEST_ASSERT(buffer.isActive(), "Buffer active after begin()");
    TEST_ASSERT(buffer.getLineCount() == 1, "Buffer has one line after begin()");
    TEST_ASSERT(buffer.getRawSize() == 0, "Buffer raw size is 0 after begin()");

    buffer.reset();
    TEST_ASSERT(!buffer.isActive(), "Buffer inactive after reset()");
}

// ============================================================================
// Test: MessageBuffer Single Key Input
// ============================================================================
void test_messagebuffer_single_key() {
    TEST_SECTION("MessageBuffer Single Key Input");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);

    // Add single character
    bool result = buffer.addKey('H', 1000);
    TEST_ASSERT(result, "First key added successfully");
    TEST_ASSERT(buffer.getRawSize() == 1, "Raw size is 1 after one key");

    // Add more characters
    result = buffer.addKey('i', 1100);
    TEST_ASSERT(result, "Second key added successfully");
    TEST_ASSERT(buffer.getRawSize() == 2, "Raw size is 2 after two keys");
}

// ============================================================================
// Test: MessageBuffer AddKeys Convenience Method
// ============================================================================
void test_messagebuffer_addkeys() {
    TEST_SECTION("MessageBuffer AddKeys Method");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);

    size_t added = buffer.addKeys("Hello World", 1000);
    TEST_ASSERT(added == 11, "addKeys returns correct count");
    TEST_ASSERT(buffer.getRawSize() == 11, "Raw size matches added characters");
}

// ============================================================================
// Test: MessageBuffer Enter Key Creates New Line
// ============================================================================
void test_messagebuffer_enter_key() {
    TEST_SECTION("MessageBuffer Enter Key Creates New Line");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);

    // Add first line
    buffer.addKeys("Line 1", 1000);
    TEST_ASSERT(buffer.getLineCount() == 1, "One line before Enter");

    // Press Enter
    buffer.addKey('\n', 2000);
    TEST_ASSERT(buffer.getLineCount() == 2, "Two lines after Enter");

    // Add second line
    buffer.addKeys("Line 2", 2100);
    TEST_ASSERT(buffer.getRawSize() == 12, "Total raw size is correct");
}

// ============================================================================
// Test: MessageBuffer Backspace Handling
// ============================================================================
void test_messagebuffer_backspace() {
    TEST_SECTION("MessageBuffer Backspace Handling");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);

    buffer.addKeys("Hello", 1000);
    TEST_ASSERT(buffer.getRawSize() == 5, "Raw size is 5");

    buffer.addKey('\b', 1100);  // Backspace
    TEST_ASSERT(buffer.getRawSize() == 4, "Raw size is 4 after backspace");

    buffer.addKey('\b', 1200);  // Another backspace
    TEST_ASSERT(buffer.getRawSize() == 3, "Raw size is 3 after second backspace");
}

// ============================================================================
// Test: MessageBuffer Flush and Packet Generation
// ============================================================================
void test_messagebuffer_flush() {
    TEST_SECTION("MessageBuffer Flush and Packet Generation");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    buffer.begin(1704067200);
    buffer.addKeys("Hello World", 1000);

    uint8_t packetsSent = buffer.flush();

    TEST_ASSERT(packetsSent == 1, "One packet sent");
    TEST_ASSERT(packetCallbackCount == 1, "Callback called once");
    TEST_ASSERT(lastIsFinal, "Packet marked as final");
    TEST_ASSERT(lastPacketLen > 0, "Packet has data");
    TEST_ASSERT(!buffer.isActive(), "Buffer inactive after flush");
}

// ============================================================================
// Test: MessageBuffer Empty Flush
// ============================================================================
void test_messagebuffer_empty_flush() {
    TEST_SECTION("MessageBuffer Empty Flush");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    buffer.begin(1704067200);
    // Don't add any keys

    uint8_t packetsSent = buffer.flush();

    TEST_ASSERT(packetsSent == 0, "No packets sent for empty buffer");
    TEST_ASSERT(packetCallbackCount == 0, "Callback not called for empty buffer");
}

// ============================================================================
// Test: MessageBuffer Timeout Check
// ============================================================================
void test_messagebuffer_timeout() {
    TEST_SECTION("MessageBuffer Timeout Check");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.flushTimeoutMs = 5000;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);
    buffer.addKeys("Hello", 1000);

    // Check timeout before it should trigger
    TEST_ASSERT(!buffer.checkTimeout(4000), "No timeout at 3 seconds");

    // Check timeout after it should trigger
    TEST_ASSERT(buffer.checkTimeout(7000), "Timeout triggers at 6 seconds");
}

// ============================================================================
// Test: MessageBuffer 200-byte Threshold
// ============================================================================
void test_messagebuffer_threshold() {
    TEST_SECTION("MessageBuffer 200-byte Threshold");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    buffer.begin(1704067200);

    // Add characters up to near threshold
    for (int i = 0; i < 195; i++) {
        buffer.addKey('a', 1000 + i);
    }

    TEST_ASSERT(buffer.getRawSize() == 195, "195 chars added before threshold");
    TEST_ASSERT(packetCallbackCount == 0, "No packet sent yet");

    // Add more to trigger threshold
    for (int i = 0; i < 10; i++) {
        buffer.addKey('b', 2000 + i);
    }

    // Should have auto-flushed
    TEST_ASSERT(packetCallbackCount >= 1, "Packet sent when threshold reached");
}

// ============================================================================
// Test: MessageBuffer Multiple Lines with Delta
// ============================================================================
void test_messagebuffer_multiline_delta() {
    TEST_SECTION("MessageBuffer Multiple Lines with Delta");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);

    // First line at t=1000ms
    buffer.addKeys("First", 1000);

    // Enter at t=2000ms
    buffer.addKey('\n', 2000);

    // Second line at t=2100ms
    buffer.addKeys("Second", 2100);

    // Enter at t=3000ms
    buffer.addKey('\n', 3000);

    // Third line at t=3100ms
    buffer.addKeys("Third", 3100);

    TEST_ASSERT(buffer.getLineCount() == 3, "Three lines in buffer");

    resetTestState();
    buffer.flush();

    TEST_ASSERT(packetCallbackCount == 1, "One packet sent");
    TEST_ASSERT(lastPacketLen > 0, "Packet has data");
}

// ============================================================================
// Test: MessageBuffer Batch ID Incrementing
// ============================================================================
void test_messagebuffer_batch_id() {
    TEST_SECTION("MessageBuffer Batch ID Incrementing");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    // First batch
    buffer.begin(1704067200);
    buffer.addKeys("Batch 1", 1000);
    buffer.flush();
    uint16_t batch1 = lastBatchId;

    // Second batch
    buffer.begin(1704067201);
    buffer.addKeys("Batch 2", 1000);
    buffer.flush();
    uint16_t batch2 = lastBatchId;

    // Third batch
    buffer.begin(1704067202);
    buffer.addKeys("Batch 3", 1000);
    buffer.flush();
    uint16_t batch3 = lastBatchId;

    TEST_ASSERT(batch2 == batch1 + 1, "Batch ID increments by 1");
    TEST_ASSERT(batch3 == batch2 + 1, "Batch ID continues incrementing");
}

// ============================================================================
// Test: MessageBuffer RAM Usage
// ============================================================================
void test_messagebuffer_ram_usage() {
    TEST_SECTION("MessageBuffer RAM Usage");

    stechat::MessageBuffer buffer;
    size_t ramUsage = buffer.getRAMUsage();

    TEST_ASSERT(ramUsage > 0, "RAM usage is non-zero");
    TEST_ASSERT(ramUsage < 100000, "RAM usage is reasonable (< 100KB)");

    printf("  [INFO] Reported RAM usage: %zu bytes\n", ramUsage);
}

// ============================================================================
// Test: MessageBuffer Double-Flush Prevention
// ============================================================================
void test_messagebuffer_double_flush() {
    TEST_SECTION("MessageBuffer Double-Flush Prevention");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    buffer.begin(1704067200);
    buffer.addKeys("Test data", 1000);

    buffer.flush();
    int countAfterFirst = packetCallbackCount;

    buffer.flush();
    buffer.flush();

    TEST_ASSERT(packetCallbackCount == countAfterFirst,
                "Multiple flush calls don't send extra packets");
}

// ============================================================================
// Test: MessageBuffer Carriage Return Same as Newline
// ============================================================================
void test_messagebuffer_carriage_return() {
    TEST_SECTION("MessageBuffer Carriage Return Handling");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    buffer.begin(1704067200);

    buffer.addKeys("Line 1", 1000);
    buffer.addKey('\r', 2000);  // Carriage return
    buffer.addKeys("Line 2", 2100);

    TEST_ASSERT(buffer.getLineCount() == 2, "CR creates new line like LF");
}

// ============================================================================
// Test: MessageBuffer Inactive State Rejection
// ============================================================================
void test_messagebuffer_inactive_rejection() {
    TEST_SECTION("MessageBuffer Inactive State Rejection");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    // Don't call begin()

    bool result = buffer.addKey('x', 1000);
    TEST_ASSERT(!result, "addKey rejected when inactive");

    size_t added = buffer.addKeys("test", 1000);
    TEST_ASSERT(added == 0, "addKeys returns 0 when inactive");
}

// ============================================================================
// Main Test Runner
// ============================================================================
int main() {
    printf("\n");
    printf("========================================\n");
    printf("  StEChat Compressor Unit Tests\n");
    printf("========================================\n");

    // Unishox2 tests
    test_unishox2_basic_compression();
    test_unishox2_bounded_api();
    test_unishox2_roundtrip();

    // MessageBuffer tests
    test_messagebuffer_begin();
    test_messagebuffer_single_key();
    test_messagebuffer_addkeys();
    test_messagebuffer_enter_key();
    test_messagebuffer_backspace();
    test_messagebuffer_flush();
    test_messagebuffer_empty_flush();
    test_messagebuffer_timeout();
    test_messagebuffer_threshold();
    test_messagebuffer_multiline_delta();
    test_messagebuffer_batch_id();
    test_messagebuffer_ram_usage();
    test_messagebuffer_double_flush();
    test_messagebuffer_carriage_return();
    test_messagebuffer_inactive_rejection();

    // Print summary
    printf("\n========================================\n");
    printf("  Test Results Summary\n");
    printf("========================================\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
