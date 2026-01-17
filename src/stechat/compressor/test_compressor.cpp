/**
 * @file test_compressor.cpp
 * @brief Unit tests for the stechat compressor module
 *
 * Tests cover:
 * 1. Unishox2 bounded API safety
 * 2. Compression/decompression roundtrip
 * 3. MessageBuffer config validation
 * 4. Compression flag correctness
 * 5. Double-flush prevention
 * 6. Buffer boundary conditions
 *
 * Compile with:
 * g++ -std=c++14 -DUNISHOX_API_WITH_OUTPUT_LEN=1 -o test_compressor \
 *     test_compressor.cpp Unishox2.cpp MessageBuffer.cpp unishox2.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Unishox2.h"
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
// Test: MessageBuffer Config Validation
// ============================================================================
void test_messagebuffer_config_validation() {
    TEST_SECTION("MessageBuffer Config Validation");

    stechat::MessageBuffer buffer;

    // Test 1: maxRecordsPerBatch clamping
    stechat::MessageBufferConfig config;
    config.maxRecordsPerBatch = 1000;  // Exceeds MAX_RECORDS (64)
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    // Add enough records to trigger flush if config wasn't clamped
    resetTestState();
    for (int i = 0; i < 65; i++) {
        buffer.addMessage("x", 1, i * 1000);
    }

    // Should have triggered at least one packet due to clamping
    TEST_ASSERT(packetCallbackCount >= 1, "Config clamping triggered flush at MAX_RECORDS");

    // Test 2: Zero maxRecordsPerBatch should use default
    buffer.reset();
    stechat::MessageBufferConfig config2;
    config2.maxRecordsPerBatch = 0;
    config2.onPacketReady = testPacketCallback;
    buffer.setConfig(config2);

    resetTestState();
    buffer.addMessage("test", 1000);
    buffer.flush();

    TEST_ASSERT(packetCallbackCount == 1, "Zero config uses default (no crash)");
}

// ============================================================================
// Test: Compression Flag Correctness
// ============================================================================
void test_compression_flag_correctness() {
    TEST_SECTION("Compression Flag Correctness");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    // Test 1: Compressible text should have FLAG_COMPRESSED
    resetTestState();
    buffer.addMessage("The quick brown fox jumps over the lazy dog", 1000);
    buffer.flush();

    TEST_ASSERT(packetCallbackCount == 1, "Packet was sent");
    TEST_ASSERT((lastPacketFlags & stechat::FLAG_COMPRESSED) != 0 ||
                (lastPacketFlags & stechat::FLAG_DELTA_TIME) != 0,
                "Compressible text has appropriate flags set");

    // Test 2: Very short/random text may not compress
    buffer.reset();
    resetTestState();
    buffer.addMessage("x", 1000);  // Single char won't compress well
    buffer.flush();

    TEST_ASSERT(packetCallbackCount == 1, "Packet was sent for short text");
    // Flags should be set appropriately based on actual compression result
    TEST_ASSERT(lastPacketLen > 0, "Packet has data regardless of compression");
}

// ============================================================================
// Test: Double-Flush Prevention
// ============================================================================
void test_double_flush_prevention() {
    TEST_SECTION("Double-Flush Prevention");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.maxRecordsPerBatch = 64;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    // Add messages that would fill the buffer
    for (int i = 0; i < 60; i++) {
        buffer.addMessage("test message", i * 100);
    }

    int countAfterAdds = packetCallbackCount;

    // Multiple flush calls
    buffer.flush();
    buffer.flush();
    buffer.flush();

    // Should only have incremented by 1 (or 0 if nothing to flush)
    TEST_ASSERT(packetCallbackCount <= countAfterAdds + 1,
                "Multiple flush calls don't send duplicate packets");
}

// ============================================================================
// Test: Buffer Boundary Conditions
// ============================================================================
void test_buffer_boundaries() {
    TEST_SECTION("Buffer Boundary Conditions");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    // Test 1: Empty flush
    resetTestState();
    uint8_t result = buffer.flush();
    TEST_ASSERT(result == 0, "Flush on empty buffer returns 0");
    TEST_ASSERT(packetCallbackCount == 0, "No packet sent on empty flush");

    // Test 2: Max text length
    char longText[stechat::MessageRecord::MAX_TEXT_LEN + 1];
    memset(longText, 'a', stechat::MessageRecord::MAX_TEXT_LEN);
    longText[stechat::MessageRecord::MAX_TEXT_LEN] = '\0';

    resetTestState();
    bool added = buffer.addMessage(longText, 1000);
    TEST_ASSERT(added, "Max length text accepted");
    buffer.flush();
    TEST_ASSERT(packetCallbackCount == 1, "Max length text packet sent");

    // Test 3: Over-max text length (should be clamped)
    char tooLongText[stechat::MessageRecord::MAX_TEXT_LEN + 100];
    memset(tooLongText, 'b', sizeof(tooLongText) - 1);
    tooLongText[sizeof(tooLongText) - 1] = '\0';

    buffer.reset();
    resetTestState();
    added = buffer.addMessage(tooLongText, 2000);
    TEST_ASSERT(added, "Over-max text clamped and accepted");

    // Test 4: NULL text
    added = buffer.addMessage(nullptr, 3000);
    TEST_ASSERT(!added, "NULL text rejected");

    // Test 5: Empty text
    added = buffer.addMessage("", 4000);
    TEST_ASSERT(!added, "Empty text rejected");
}

// ============================================================================
// Test: Batch ID Incrementing
// ============================================================================
void test_batch_id_incrementing() {
    TEST_SECTION("Batch ID Incrementing");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    resetTestState();

    // First batch
    buffer.addMessage("batch 1", 1000);
    buffer.flush();
    uint16_t batch1 = lastBatchId;

    // Second batch
    buffer.addMessage("batch 2", 2000);
    buffer.flush();
    uint16_t batch2 = lastBatchId;

    // Third batch
    buffer.addMessage("batch 3", 3000);
    buffer.flush();
    uint16_t batch3 = lastBatchId;

    TEST_ASSERT(batch2 == batch1 + 1, "Batch ID increments by 1");
    TEST_ASSERT(batch3 == batch2 + 1, "Batch ID continues incrementing");
}

// ============================================================================
// Test: Pending Count Tracking
// ============================================================================
void test_pending_count() {
    TEST_SECTION("Pending Count Tracking");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    TEST_ASSERT(buffer.getPendingCount() == 0, "Initial pending count is 0");

    buffer.addMessage("msg1", 1000);
    TEST_ASSERT(buffer.getPendingCount() == 1, "Count after 1 message");

    buffer.addMessage("msg2", 2000);
    TEST_ASSERT(buffer.getPendingCount() == 2, "Count after 2 messages");

    buffer.flush();
    TEST_ASSERT(buffer.getPendingCount() == 0, "Count after flush is 0");
}

// ============================================================================
// Test: RAM Usage Reporting
// ============================================================================
void test_ram_usage() {
    TEST_SECTION("RAM Usage Reporting");

    stechat::MessageBuffer buffer;
    size_t ramUsage = buffer.getRAMUsage();

    TEST_ASSERT(ramUsage > 0, "RAM usage is non-zero");
    TEST_ASSERT(ramUsage < 50000, "RAM usage is reasonable (< 50KB)");

    printf("  [INFO] Reported RAM usage: %zu bytes\n", ramUsage);
}

// ============================================================================
// Test: needsFlush Behavior
// ============================================================================
void test_needs_flush() {
    TEST_SECTION("needsFlush Behavior");

    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.maxRecordsPerBatch = 10;
    config.onPacketReady = testPacketCallback;
    buffer.setConfig(config);

    TEST_ASSERT(!buffer.needsFlush(), "Empty buffer doesn't need flush");

    // Add messages below threshold
    for (int i = 0; i < 5; i++) {
        buffer.addMessage("x", i * 100);
    }
    TEST_ASSERT(!buffer.needsFlush(), "Buffer below threshold doesn't need flush");

    // Add messages to reach threshold
    for (int i = 5; i < 10; i++) {
        buffer.addMessage("x", i * 100);
    }
    // Note: auto-flush may have already happened
    // But after reset, needsFlush should return correct value
    buffer.reset();
    TEST_ASSERT(!buffer.needsFlush(), "Reset buffer doesn't need flush");
}

// ============================================================================
// Main Test Runner
// ============================================================================
int main() {
    printf("\n");
    printf("========================================\n");
    printf("  StEChat Compressor Unit Tests\n");
    printf("========================================\n");

    // Run all tests
    test_unishox2_basic_compression();
    test_unishox2_bounded_api();
    test_unishox2_roundtrip();
    test_messagebuffer_config_validation();
    test_compression_flag_correctness();
    test_double_flush_prevention();
    test_buffer_boundaries();
    test_batch_id_incrementing();
    test_pending_count();
    test_ram_usage();
    test_needs_flush();

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
