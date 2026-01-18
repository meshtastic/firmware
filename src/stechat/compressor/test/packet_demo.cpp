/**
 * @file packet_demo.cpp
 * @brief Demonstrates the packet format created by MessageBuffer
 */

#include <stdio.h>
#include <string.h>
#include "Compressor.h"
#include "MessageBuffer.h"

// Store packet for analysis
static uint8_t capturedPacket[256];
static size_t capturedLen = 0;

void capturePacket(const uint8_t* data, size_t len,
                   uint16_t batchId, uint8_t packetNum, bool isFinal) {
    memcpy(capturedPacket, data, len);
    capturedLen = len;
    printf("Callback: batchId=%u, packetNum=%u, isFinal=%d, len=%zu\n",
           batchId, packetNum, isFinal, len);
}

void printHex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

void parsePacketHeader(const uint8_t* data) {
    uint16_t batchId = data[0] | (data[1] << 8);
    uint32_t timestamp = data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24);
    uint8_t flags = data[6];
    uint8_t lineCount = data[7];

    printf("\n=== PACKET HEADER (8 bytes) ===\n");
    printf("Bytes [0-1]: Batch ID     = %u (0x%02X 0x%02X)\n", batchId, data[0], data[1]);
    printf("Bytes [2-5]: Timestamp    = %u (Unix epoch)\n", timestamp);
    printf("Byte  [6]:   Flags        = 0x%02X\n", flags);
    printf("             - HAS_MORE   = %s\n", (flags & 0x01) ? "yes" : "no");
    printf("             - COMPRESSED = %s\n", (flags & 0x02) ? "yes" : "no");
    printf("             - DELTA_TIME = %s\n", (flags & 0x04) ? "yes" : "no");
    printf("Byte  [7]:   Line Count   = %u\n", lineCount);
    printf("Bytes [8+]:  Payload      = (compressed or raw line data)\n");
}

int main() {
    stechat::MessageBuffer buffer;
    stechat::MessageBufferConfig config;
    config.onPacketReady = capturePacket;
    buffer.setConfig(config);

    // Example 1: Single line
    printf("\n");
    printf("========================================\n");
    printf("  EXAMPLE 1: Single Line\n");
    printf("========================================\n");
    printf("\nInput: \"Hello World\" at timestamp 1704067200 (2024-01-01 00:00:00 UTC)\n");

    buffer.begin(1704067200);  // 2024-01-01 00:00:00 UTC
    buffer.addKeys("Hello World", 1000);
    buffer.flush();

    printf("\nRaw packet bytes (%zu total):\n", capturedLen);
    printHex(capturedPacket, capturedLen);
    parsePacketHeader(capturedPacket);

    // Example 2: Multiple lines with delta timestamps
    printf("\n");
    printf("========================================\n");
    printf("  EXAMPLE 2: Multiple Lines with Deltas\n");
    printf("========================================\n");
    printf("\nInput:\n");
    printf("  Line 1: \"Hi\" at t=0ms\n");
    printf("  Line 2: \"How are you?\" at t=2000ms (Enter pressed)\n");
    printf("  Line 3: \"Fine thanks\" at t=5000ms (Enter pressed)\n");
    printf("  Base timestamp: 1704067200\n");

    buffer.begin(1704067200);
    buffer.addKeys("Hi", 0);
    buffer.addKey('\n', 2000);  // Enter at 2 seconds
    buffer.addKeys("How are you?", 2100);
    buffer.addKey('\n', 5000);  // Enter at 5 seconds
    buffer.addKeys("Fine thanks", 5100);
    buffer.flush();

    printf("\nRaw packet bytes (%zu total):\n", capturedLen);
    printHex(capturedPacket, capturedLen);
    parsePacketHeader(capturedPacket);

    printf("\n=== PAYLOAD FORMAT (after header) ===\n");
    printf("Each line is encoded as:\n");
    printf("  [timestamp_varint][text_length_byte][text_bytes...]\n");
    printf("\nLine 1: timestamp=1704067200 (absolute), text=\"Hi\"\n");
    printf("Line 2: timestamp=delta_ms from start, text=\"How are you?\"\n");
    printf("Line 3: timestamp=delta_ms from start, text=\"Fine thanks\"\n");
    printf("\nNote: Payload is Unishox2 compressed if smaller than raw.\n");

    // Example 3: Show compression ratio
    printf("\n");
    printf("========================================\n");
    printf("  EXAMPLE 3: Compression Effectiveness\n");
    printf("========================================\n");

    const char* longMessage = "The quick brown fox jumps over the lazy dog. "
                              "This is a longer message to show compression.";

    buffer.begin(1704067200);
    buffer.addKeys(longMessage, 1000);
    buffer.flush();

    size_t rawLen = strlen(longMessage);
    size_t payloadLen = capturedLen - 8;  // Subtract header

    printf("\nInput: \"%s\"\n", longMessage);
    printf("\nRaw text length:    %zu bytes\n", rawLen);
    printf("Compressed payload: %zu bytes\n", payloadLen);
    printf("Total packet size:  %zu bytes (8 header + %zu payload)\n", capturedLen, payloadLen);
    printf("Compression ratio:  %.1f%%\n", (1.0 - (double)payloadLen / rawLen) * 100);

    printf("\nRaw packet bytes:\n");
    printHex(capturedPacket, capturedLen);

    return 0;
}
