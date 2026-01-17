/**
 * @file KeystrokeBuffer.h
 * @brief Circular buffer for keystroke compression and batching
 *
 * Collects keystrokes, compresses them using Unishox2, and
 * packs them into Meshtastic-sized packets (190 bytes max).
 *
 * Features:
 * - Circular buffer for keystroke storage
 * - On-the-fly compression using Unishox2
 * - Automatic packet building when buffer is full
 * - Delta timestamp encoding (varint)
 * - Batch ID tracking for packet reassembly
 */

#ifndef TAKEOVER_KEYSTROKE_BUFFER_H
#define TAKEOVER_KEYSTROKE_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include "Unishox2.h"

namespace takeover {

/**
 * @brief Callback for when a packet is ready to send
 * @param data Packet data
 * @param len Packet length
 * @param batchId Batch identifier for this packet sequence
 * @param packetNum Packet number within this batch (0-based)
 * @param isFinal True if this is the last packet in the batch
 */
typedef void (*PacketReadyCallback)(const uint8_t* data, size_t len,
                                    uint16_t batchId, uint8_t packetNum, bool isFinal);

/**
 * @brief Configuration for KeystrokeBuffer
 */
struct KeystrokeBufferConfig {
    /// Maximum packet payload size (Meshtastic limit)
    uint16_t maxPacketPayload = 190;

    /// Packet header size (batch ID, timestamp, flags, count)
    uint8_t packetHeaderSize = 8;

    /// Maximum time between keystrokes before auto-flush (ms)
    uint32_t autoFlushTimeoutMs = 300000;  // 5 minutes

    /// Maximum records per batch before auto-flush
    uint16_t maxRecordsPerBatch = 1000;

    /// Callback when packet is ready
    PacketReadyCallback onPacketReady = nullptr;
};

/**
 * @brief Record stored in the buffer
 */
struct KeystrokeRecord {
    uint32_t timestamp;     ///< Unix timestamp (seconds) or delta (ms)
    char text[64];          ///< Keystroke text
    uint8_t textLen;        ///< Text length
    bool isAbsolute;        ///< True if timestamp is absolute (first record)
};

/**
 * @brief Circular buffer for keystroke compression
 *
 * Usage:
 * 1. Configure with setConfig() or use defaults
 * 2. Call addKeystroke() for each captured keystroke
 * 3. Implement PacketReadyCallback to receive compressed packets
 * 4. Call flush() to force sending remaining data
 */
class KeystrokeBuffer {
public:
    /// Maximum records in circular buffer
    static constexpr size_t MAX_RECORDS = 64;

    /// Maximum packet data size
    static constexpr size_t MAX_PACKET_DATA = 200;

    /**
     * @brief Constructor
     */
    KeystrokeBuffer();

    /**
     * @brief Set configuration
     * @param config Configuration structure
     */
    void setConfig(const KeystrokeBufferConfig& config);

    /**
     * @brief Add a keystroke to the buffer
     *
     * @param text Keystroke text (null-terminated)
     * @param timestampMs Timestamp in milliseconds (or 0 to use internal timing)
     * @return True if keystroke was added successfully
     */
    bool addKeystroke(const char* text, uint32_t timestampMs = 0);

    /**
     * @brief Add a keystroke with explicit length
     *
     * @param text Keystroke text
     * @param textLen Text length
     * @param timestampMs Timestamp in milliseconds
     * @return True if keystroke was added
     */
    bool addKeystroke(const char* text, size_t textLen, uint32_t timestampMs);

    /**
     * @brief Flush all pending data
     *
     * Forces compression and sending of any buffered keystrokes.
     * Call this when Enter is pressed or on timeout.
     *
     * @return Number of packets sent
     */
    uint8_t flush();

    /**
     * @brief Check if buffer needs flushing
     *
     * @return True if auto-flush timeout has elapsed or buffer is full
     */
    bool needsFlush() const;

    /**
     * @brief Get number of pending records
     * @return Record count
     */
    size_t getPendingCount() const { return recordCount_; }

    /**
     * @brief Get current batch ID
     * @return Batch ID
     */
    uint16_t getCurrentBatchId() const { return batchId_; }

    /**
     * @brief Reset buffer state
     */
    void reset();

    /**
     * @brief Get RAM usage estimate
     * @return RAM usage in bytes
     */
    size_t getRAMUsage() const;

private:
    // Configuration
    KeystrokeBufferConfig config_;

    // Circular buffer
    KeystrokeRecord records_[MAX_RECORDS];
    size_t recordHead_;     ///< Next write position
    size_t recordTail_;     ///< Next read position
    size_t recordCount_;    ///< Current record count

    // Batch state
    uint16_t batchId_;
    uint32_t batchStartTime_;   ///< First keystroke timestamp in batch
    uint32_t lastKeystrokeTime_;

    // Packet building
    uint8_t packetBuffer_[MAX_PACKET_DATA];
    size_t packetLen_;
    uint8_t packetRecordCount_;
    uint8_t packetNum_;

    // Compressor
    Unishox2 compressor_;

    // Internal methods
    bool compressAndAddToPacket(const KeystrokeRecord& record);
    void finalizePacket(bool isFinal);
    size_t encodeVarint(uint32_t value, uint8_t* buf);
    uint32_t decodeVarint(const uint8_t* buf, size_t* bytesRead);

    /**
     * @brief Packet header structure
     *
     * Format (8 bytes):
     * [0-1] Batch ID (uint16_t, little-endian)
     * [2-5] Base timestamp (uint32_t, little-endian, Unix seconds)
     * [6]   Flags (bit 0: has more packets, bit 1-7: reserved)
     * [7]   Record count in this packet
     */
    void writePacketHeader(uint32_t baseTimestamp, uint8_t flags, uint8_t count);
};

/**
 * @brief Packet flags
 */
enum PacketFlags : uint8_t {
    FLAG_HAS_MORE = 0x01,       ///< More packets follow in this batch
    FLAG_COMPRESSED = 0x02,     ///< Data is Unishox2 compressed
    FLAG_DELTA_TIME = 0x04,     ///< Timestamps are delta-encoded
};

} // namespace takeover

#endif // TAKEOVER_KEYSTROKE_BUFFER_H
