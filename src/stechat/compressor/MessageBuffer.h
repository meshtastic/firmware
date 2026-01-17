/**
 * @file MessageBuffer.h
 * @brief Character-by-character input buffer with compression
 *
 * Collects keystrokes, organizes into lines with timestamps,
 * compresses at 200-byte threshold, and packs into packets.
 *
 * Architecture:
 * - Buffer initializes with Unix epoch timestamp
 * - Keys are added one at a time via addKey()
 * - Enter key creates new line with delta timestamp
 * - At RAW_THRESHOLD bytes, compression is triggered
 * - Compressed data is checked to see if more fits
 *
 * NASA JPL Power of 10 Rules Compliance:
 * - Rule 1: No goto, setjmp, or recursion
 * - Rule 2: All loops have fixed upper bounds
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
 */
struct MessageBufferConfig {
    /// Maximum packet payload size (Meshtastic limit)
    uint16_t maxPacketPayload = 190;

    /// Packet header size (batch ID, timestamp, flags, count)
    uint8_t packetHeaderSize = 8;

    /// Timeout in milliseconds before auto-flush (0 = disabled)
    uint32_t flushTimeoutMs = 5000;

    /// Callback when packet is ready (may be nullptr)
    PacketReadyCallback onPacketReady = nullptr;
};

/**
 * @brief Line record stored in the buffer
 *
 * Each line has a timestamp and text content.
 * First line has absolute timestamp, subsequent use delta.
 */
struct LineRecord {
    /// Maximum text length per line (NASA Rule 2: fixed bound)
    static constexpr size_t MAX_LINE_LEN = 200;

    uint32_t timestamp;              ///< Unix timestamp (seconds) or delta (ms)
    char text[MAX_LINE_LEN + 1];     ///< Line text (null-terminated)
    uint16_t textLen;                ///< Text length (0 to MAX_LINE_LEN)
    bool isAbsolute;                 ///< True if timestamp is absolute (first line)
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
 * @brief Character-by-character input buffer with compression
 *
 * Thread Safety: Not thread-safe. External synchronization required.
 *
 * Usage:
 * 1. Call begin() with initial Unix timestamp
 * 2. Call addKey() for each character typed
 * 3. Enter key triggers newLine() internally
 * 4. At RAW_THRESHOLD, compression and packet sending occurs
 * 5. Call flush() to force sending remaining data
 */
class MessageBuffer {
public:
    /// Raw buffer threshold for compression (NASA Rule 2: fixed bound)
    static constexpr size_t RAW_THRESHOLD = 200;

    /// Maximum lines in buffer (NASA Rule 2: fixed bound)
    static constexpr size_t MAX_LINES = 32;

    /// Maximum packet data size (NASA Rule 2: fixed bound)
    static constexpr size_t MAX_PACKET_DATA = 200;

    /// Maximum varint encoding size in bytes
    static constexpr size_t MAX_VARINT_LEN = 5;

    /// Maximum compressed buffer size
    static constexpr size_t MAX_COMPRESSED_LEN = 256;

    /**
     * @brief Default constructor
     *
     * Initializes all state to safe default values.
     * Call begin() before use.
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
     * @brief Begin a new input session
     *
     * Initializes the buffer with the current Unix timestamp.
     * Must be called before addKey().
     *
     * @param unixTimestamp Current Unix time in seconds
     */
    void begin(uint32_t unixTimestamp);

    /**
     * @brief Add a single key/character to the buffer
     *
     * @param c Character to add
     * @param timestampMs Current time in milliseconds (for delta calculation)
     * @return True if character was added, false if buffer full or error
     *
     * Special characters:
     * - '\n' or '\r': Creates new line with delta timestamp
     * - '\b': Backspace, removes last character if any
     * - Other: Added to current line
     */
    bool addKey(char c, uint32_t timestampMs);

    /**
     * @brief Add multiple characters at once (convenience method)
     *
     * @param text Characters to add (null-terminated)
     * @param timestampMs Current time in milliseconds
     * @return Number of characters successfully added
     */
    size_t addKeys(const char* text, uint32_t timestampMs);

    /**
     * @brief Check if timeout has elapsed and flush is needed
     *
     * @param currentTimeMs Current time in milliseconds
     * @return True if flush should be called
     */
    bool checkTimeout(uint32_t currentTimeMs);

    /**
     * @brief Flush all pending data
     *
     * Compresses and sends any buffered data.
     *
     * @return Number of packets sent (0 to 255)
     */
    uint8_t flush();

    /**
     * @brief Get current raw buffer size
     * @return Total bytes of raw text in buffer
     */
    size_t getRawSize() const;

    /**
     * @brief Get number of lines in buffer
     * @return Line count (0 to MAX_LINES)
     */
    size_t getLineCount() const;

    /**
     * @brief Get current batch ID
     * @return Batch ID
     */
    uint16_t getCurrentBatchId() const;

    /**
     * @brief Reset buffer state
     *
     * Clears all pending data. Must call begin() again before use.
     */
    void reset();

    /**
     * @brief Get RAM usage estimate
     * @return RAM usage in bytes
     */
    size_t getRAMUsage() const;

    /**
     * @brief Check if buffer has been started
     * @return True if begin() was called
     */
    bool isActive() const { return isActive_; }

private:
    // Configuration
    MessageBufferConfig config_;

    // Session state
    bool isActive_;                  ///< True if begin() was called
    uint32_t startTimestamp_;        ///< Unix timestamp when session started
    uint32_t lastKeyTime_;           ///< Last key input time (ms)

    // Line buffer (NASA Rule 3: fixed-size array)
    LineRecord lines_[MAX_LINES];
    size_t lineCount_;               ///< Number of lines (0 to MAX_LINES)
    size_t currentLineLen_;          ///< Current line text length

    // Batch state
    uint16_t batchId_;

    // Packet building (NASA Rule 3: fixed-size buffer)
    uint8_t packetBuffer_[MAX_PACKET_DATA];
    size_t packetLen_;
    uint8_t packetNum_;

    // Re-entry guard (NASA Rule 1: prevent recursion)
    bool inFlush_;

    // Compressor instance
    Unishox2 compressor_;

    /**
     * @brief Create a new line with delta timestamp
     *
     * @param timestampMs Current time in milliseconds
     * @return True if new line created, false if at MAX_LINES
     */
    bool newLine(uint32_t timestampMs);

    /**
     * @brief Compress all lines into packet buffer
     *
     * @return Size of compressed data, or 0 on error
     */
    size_t compressLines();

    /**
     * @brief Finalize and send the current packet
     *
     * @param isFinal True if this is the last packet in the batch
     */
    void finalizePacket(bool isFinal);

    /**
     * @brief Write packet header to buffer
     *
     * @param baseTimestamp Base timestamp for this batch
     * @param flags Packet flags
     * @param lineCount Number of lines in this packet
     */
    void writePacketHeader(uint32_t baseTimestamp, uint8_t flags, uint8_t lineCount);

    /**
     * @brief Encode a 32-bit value as varint
     *
     * @param value Value to encode
     * @param buf Output buffer (must have at least MAX_VARINT_LEN bytes)
     * @return Number of bytes written
     */
    static size_t encodeVarint(uint32_t value, uint8_t* buf);

    /**
     * @brief Calculate total raw text size
     * @return Sum of all line lengths
     */
    size_t calculateRawSize() const;
};

} // namespace stechat

#endif // STECHAT_MESSAGE_BUFFER_H
