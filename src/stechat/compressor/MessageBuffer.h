/**
 * @file MessageBuffer.h
 * @brief Circular buffer for message compression and batching
 *
 * Collects message input, compresses using Unishox2, and
 * packs into Meshtastic-sized packets (190 bytes max).
 *
 * NASA JPL Power of 10 Rules Compliance:
 * - Rule 1: No goto, setjmp, or recursion
 * - Rule 2: All loops have fixed upper bounds (MAX_RECORDS, MAX_PACKET_DATA)
 * - Rule 3: No dynamic memory allocation (fixed-size arrays)
 * - Rule 4: Functions kept under 60 lines
 * - Rule 5: Minimum 2 assertions per function
 * - Rule 6: Data declared at smallest scope
 * - Rule 7: All return values checked
 * - Rule 8: Limited preprocessor use (only include guards)
 * - Rule 9: Pointer use restricted and documented
 * - Rule 10: Compiled with all warnings enabled
 */

#ifndef STECHAT_MESSAGE_BUFFER_H
#define STECHAT_MESSAGE_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "Unishox2.h"

namespace stechat {

/**
 * @brief Callback for when a packet is ready to send
 *
 * @param data Packet data (valid only during callback)
 * @param len Packet length in bytes
 * @param batchId Batch identifier for this packet sequence
 * @param packetNum Packet number within this batch (0-based)
 * @param isFinal True if this is the last packet in the batch
 *
 * @note Callback must not store the data pointer; copy if needed
 */
typedef void (*PacketReadyCallback)(const uint8_t* data, size_t len,
                                    uint16_t batchId, uint8_t packetNum, bool isFinal);

/**
 * @brief Configuration for MessageBuffer
 *
 * All fields have NASA-compliant fixed default values.
 * Config is validated in setConfig() to ensure safe operation.
 */
struct MessageBufferConfig {
    /// Maximum packet payload size (Meshtastic limit)
    uint16_t maxPacketPayload = 190;

    /// Packet header size (batch ID, timestamp, flags, count)
    uint8_t packetHeaderSize = 8;

    /// Maximum records per batch before auto-flush (must be <= MAX_RECORDS)
    /// Note: Clamped to MAX_RECORDS (64) during validation
    uint16_t maxRecordsPerBatch = 64;

    /// Callback when packet is ready (may be nullptr)
    PacketReadyCallback onPacketReady = nullptr;
};

/**
 * @brief Record stored in the buffer
 *
 * Fixed-size structure for NASA Rule 3 compliance.
 */
struct MessageRecord {
    /// Maximum text length per record (NASA Rule 2: fixed bound)
    static constexpr size_t MAX_TEXT_LEN = 63;

    uint32_t timestamp;              ///< Unix timestamp (seconds) or delta (ms)
    char text[MAX_TEXT_LEN + 1];     ///< Message text (null-terminated)
    uint8_t textLen;                 ///< Text length (0 to MAX_TEXT_LEN)
    bool isAbsolute;                 ///< True if timestamp is absolute (first record)
};

/**
 * @brief Packet flags for transmission
 */
enum PacketFlags : uint8_t {
    FLAG_HAS_MORE = 0x01,       ///< More packets follow in this batch
    FLAG_COMPRESSED = 0x02,     ///< Data is Unishox2 compressed
    FLAG_DELTA_TIME = 0x04,     ///< Timestamps are delta-encoded
};

/**
 * @brief Circular buffer for message compression
 *
 * Thread Safety: Not thread-safe. External synchronization required.
 *
 * Usage:
 * 1. Configure with setConfig() or use defaults
 * 2. Call addMessage() for each input character/string
 * 3. Implement PacketReadyCallback to receive compressed packets
 * 4. Call flush() to force sending remaining data
 */
class MessageBuffer {
public:
    /// Maximum records in circular buffer (NASA Rule 2: fixed bound)
    static constexpr size_t MAX_RECORDS = 64;

    /// Maximum packet data size (NASA Rule 2: fixed bound)
    static constexpr size_t MAX_PACKET_DATA = 200;

    /// Maximum varint encoding size in bytes
    static constexpr size_t MAX_VARINT_LEN = 5;

    /// Maximum compressed text size
    static constexpr size_t MAX_COMPRESSED_LEN = 128;

    /**
     * @brief Default constructor
     *
     * Initializes all state to safe default values.
     */
    MessageBuffer();

    /**
     * @brief Set configuration
     *
     * @param config Configuration structure
     * @pre config.maxPacketPayload >= config.packetHeaderSize + 10
     */
    void setConfig(const MessageBufferConfig& config);

    /**
     * @brief Add a message to the buffer
     *
     * @param text Message text (null-terminated, max MessageRecord::MAX_TEXT_LEN)
     * @param timestampMs Timestamp in milliseconds (or 0 to use internal timing)
     * @return True if message was added successfully, false on error
     *
     * @pre text != nullptr
     */
    bool addMessage(const char* text, uint32_t timestampMs = 0);

    /**
     * @brief Add a message with explicit length
     *
     * @param text Message text (need not be null-terminated)
     * @param textLen Text length (max MessageRecord::MAX_TEXT_LEN)
     * @param timestampMs Timestamp in milliseconds
     * @return True if message was added successfully
     *
     * @pre text != nullptr
     * @pre textLen <= MessageRecord::MAX_TEXT_LEN
     */
    bool addMessage(const char* text, size_t textLen, uint32_t timestampMs);

    /**
     * @brief Flush all pending data
     *
     * Forces compression and sending of any buffered messages.
     * Call this when Enter is pressed or on timeout.
     *
     * @return Number of packets sent (0 to 255)
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
     * @return Record count (0 to MAX_RECORDS)
     */
    size_t getPendingCount() const;

    /**
     * @brief Get current batch ID
     * @return Batch ID
     */
    uint16_t getCurrentBatchId() const;

    /**
     * @brief Reset buffer state
     *
     * Clears all pending data and resets batch ID.
     */
    void reset();

    /**
     * @brief Get RAM usage estimate
     * @return RAM usage in bytes
     */
    size_t getRAMUsage() const;

private:
    // Configuration
    MessageBufferConfig config_;

    // Circular buffer (NASA Rule 3: fixed-size array)
    MessageRecord records_[MAX_RECORDS];
    size_t recordHead_;     ///< Next write position (0 to MAX_RECORDS-1)
    size_t recordTail_;     ///< Next read position (0 to MAX_RECORDS-1)
    size_t recordCount_;    ///< Current record count (0 to MAX_RECORDS)

    // Batch state
    uint16_t batchId_;
    uint32_t batchStartTime_;   ///< First message timestamp in batch
    uint32_t lastMessageTime_;

    // Packet building (NASA Rule 3: fixed-size buffer)
    uint8_t packetBuffer_[MAX_PACKET_DATA];
    size_t packetLen_;
    uint8_t packetRecordCount_;
    uint8_t packetNum_;

    // Packet flags tracking (fix for compression flag mismatch)
    bool packetHasCompressedData_;   ///< True if any record in packet was compressed
    bool packetHasRawData_;          ///< True if any record in packet was NOT compressed

    // Re-entry guard (fix for double-flush issue)
    bool inFlush_;                   ///< True while flush() is executing

    // Compressor instance
    Unishox2 compressor_;

    /**
     * @brief Compress and add a record to the current packet
     *
     * @param record Record to compress and add
     * @return True if record was added, false if packet is full
     */
    bool compressAndAddToPacket(const MessageRecord& record);

    /**
     * @brief Finalize and send the current packet
     *
     * @param isFinal True if this is the last packet in the batch
     */
    void finalizePacket(bool isFinal);

    /**
     * @brief Encode a 32-bit value as varint
     *
     * @param value Value to encode (0 to UINT32_MAX)
     * @param buf Output buffer (must have at least MAX_VARINT_LEN bytes)
     * @return Number of bytes written (1 to MAX_VARINT_LEN)
     *
     * @pre buf != nullptr
     */
    size_t encodeVarint(uint32_t value, uint8_t* buf);

    /**
     * @brief Decode a varint from buffer
     *
     * @param buf Input buffer
     * @param maxLen Maximum bytes to read
     * @param bytesRead Output: number of bytes consumed
     * @return Decoded value
     *
     * @pre buf != nullptr
     * @pre bytesRead != nullptr
     */
    uint32_t decodeVarint(const uint8_t* buf, size_t maxLen, size_t* bytesRead);

    /**
     * @brief Write packet header to buffer
     *
     * @param baseTimestamp Base timestamp for this batch
     * @param flags Packet flags
     * @param count Record count in this packet
     */
    void writePacketHeader(uint32_t baseTimestamp, uint8_t flags, uint8_t count);
};

} // namespace stechat

#endif // STECHAT_MESSAGE_BUFFER_H
