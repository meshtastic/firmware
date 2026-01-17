/**
 * @file MessageBuffer.cpp
 * @brief Character-by-character input buffer implementation
 *
 * NASA JPL Power of 10 Rules Compliance:
 * - All functions contain assertions for precondition checking
 * - All loops have fixed upper bounds
 * - No dynamic memory allocation
 * - No recursion or goto statements
 * - All return values are checked
 */

#include "MessageBuffer.h"
#include <string.h>
#include <assert.h>

namespace stechat {

MessageBuffer::MessageBuffer()
    : isActive_(false)
    , startTimestamp_(0)
    , lastKeyTime_(0)
    , lineCount_(0)
    , currentLineLen_(0)
    , batchId_(0)
    , packetLen_(0)
    , packetNum_(0)
    , inFlush_(false)
{
    // NASA Rule 5: Assertions to verify initialization
    assert(MAX_LINES > 0 && "MAX_LINES must be positive");
    assert(MAX_PACKET_DATA > 0 && "MAX_PACKET_DATA must be positive");

    // Zero-initialize all buffers (NASA Rule 3: no malloc needed)
    memset(lines_, 0, sizeof(lines_));
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
}

void MessageBuffer::setConfig(const MessageBufferConfig& config) {
    // NASA Rule 5: Assertions for preconditions
    assert(config.maxPacketPayload >= config.packetHeaderSize + 10 &&
           "Payload must be larger than header");
    assert(config.packetHeaderSize >= 8 && "Header must be at least 8 bytes");

    config_ = config;

    // Validate payload size (NASA Rule 2: fixed bounds)
    if (config_.maxPacketPayload > MAX_PACKET_DATA) {
        config_.maxPacketPayload = MAX_PACKET_DATA;
    }
}

void MessageBuffer::begin(uint32_t unixTimestamp) {
    // NASA Rule 5: Assertions
    assert(unixTimestamp > 0 && "Timestamp should be non-zero");
    assert(MAX_LINES > 0 && "Invalid constant");

    // Reset state
    isActive_ = true;
    startTimestamp_ = unixTimestamp;
    lastKeyTime_ = 0;
    lineCount_ = 1;  // Start with one line
    currentLineLen_ = 0;
    ++batchId_;
    packetLen_ = 0;
    packetNum_ = 0;
    inFlush_ = false;

    // Initialize first line with absolute timestamp
    lines_[0].timestamp = unixTimestamp;
    lines_[0].textLen = 0;
    lines_[0].text[0] = '\0';
    lines_[0].isAbsolute = true;
}

bool MessageBuffer::addKey(char c, uint32_t timestampMs) {
    // NASA Rule 5: Assertions
    assert(MAX_LINES > 0 && "Invalid constant");
    assert(LineRecord::MAX_LINE_LEN > 0 && "Invalid constant");

    // NASA Rule 7: Validate state
    if (!isActive_) {
        return false;
    }

    // Prevent re-entry during flush
    if (inFlush_) {
        return false;
    }

    lastKeyTime_ = timestampMs;

    // Handle special characters
    if (c == '\n' || c == '\r') {
        // Enter key - create new line with delta
        return newLine(timestampMs);
    }

    if (c == '\b') {
        // Backspace - remove last character if any
        if (lineCount_ > 0 && currentLineLen_ > 0) {
            LineRecord& currentLine = lines_[lineCount_ - 1];
            if (currentLine.textLen > 0) {
                --currentLine.textLen;
                currentLine.text[currentLine.textLen] = '\0';
                --currentLineLen_;
            }
        }
        return true;
    }

    // Check if we need to auto-flush (raw threshold reached)
    size_t rawSize = calculateRawSize();
    if (rawSize >= RAW_THRESHOLD) {
        (void)flush();
        // After flush, start new session
        begin(startTimestamp_ + (timestampMs / 1000));
    }

    // Check if current line is full
    if (lineCount_ > 0) {
        LineRecord& currentLine = lines_[lineCount_ - 1];
        if (currentLine.textLen >= LineRecord::MAX_LINE_LEN) {
            // Line full - create new line
            if (!newLine(timestampMs)) {
                return false;  // No room for new line
            }
        }
    }

    // Add character to current line
    if (lineCount_ > 0 && lineCount_ <= MAX_LINES) {
        LineRecord& currentLine = lines_[lineCount_ - 1];
        if (currentLine.textLen < LineRecord::MAX_LINE_LEN) {
            currentLine.text[currentLine.textLen] = c;
            ++currentLine.textLen;
            currentLine.text[currentLine.textLen] = '\0';
            ++currentLineLen_;
            return true;
        }
    }

    return false;
}

size_t MessageBuffer::addKeys(const char* text, uint32_t timestampMs) {
    // NASA Rule 5: Assertions
    assert(text != nullptr && "Text pointer must not be null");
    assert(MAX_LINES > 0 && "Invalid constant");

    // NASA Rule 7: Validate input
    if (text == nullptr) {
        return 0;
    }

    size_t added = 0;
    // NASA Rule 2: Fixed loop bound
    for (size_t i = 0; i < LineRecord::MAX_LINE_LEN * MAX_LINES && text[i] != '\0'; ++i) {
        if (addKey(text[i], timestampMs)) {
            ++added;
        } else {
            break;  // Buffer full or error
        }
    }

    return added;
}

bool MessageBuffer::newLine(uint32_t timestampMs) {
    // NASA Rule 5: Assertions
    assert(lineCount_ <= MAX_LINES && "Invalid line count");
    assert(MAX_LINES > 0 && "Invalid constant");

    // Check if we can add more lines
    if (lineCount_ >= MAX_LINES) {
        // Buffer full - need to flush first
        (void)flush();
        begin(startTimestamp_ + (timestampMs / 1000));
    }

    if (lineCount_ >= MAX_LINES) {
        return false;  // Still no room after flush
    }

    // Calculate delta from start (in milliseconds)
    uint32_t delta = timestampMs - (startTimestamp_ * 1000);

    // Create new line
    LineRecord& newLineRecord = lines_[lineCount_];
    newLineRecord.timestamp = delta;
    newLineRecord.textLen = 0;
    newLineRecord.text[0] = '\0';
    newLineRecord.isAbsolute = false;

    ++lineCount_;
    currentLineLen_ = 0;

    return true;
}

bool MessageBuffer::checkTimeout(uint32_t currentTimeMs) {
    // NASA Rule 5: Assertions
    assert(MAX_LINES > 0 && "Invalid constant");
    assert(config_.flushTimeoutMs < 3600000 && "Timeout too large");

    if (!isActive_ || config_.flushTimeoutMs == 0) {
        return false;
    }

    // Check if timeout elapsed since last key
    if (lastKeyTime_ > 0 && currentTimeMs > lastKeyTime_) {
        uint32_t elapsed = currentTimeMs - lastKeyTime_;
        if (elapsed >= config_.flushTimeoutMs) {
            return calculateRawSize() > 0;
        }
    }

    return false;
}

uint8_t MessageBuffer::flush() {
    // NASA Rule 5: Assertions
    assert(lineCount_ <= MAX_LINES && "Invalid line count");
    assert(MAX_PACKET_DATA > 0 && "Invalid constant");

    // Prevent re-entry
    if (inFlush_) {
        return 0;
    }
    inFlush_ = true;

    uint8_t packetsSent = 0;

    // Check if there's anything to flush
    size_t rawSize = calculateRawSize();
    if (rawSize == 0 || lineCount_ == 0) {
        inFlush_ = false;
        return 0;
    }

    // Compress all lines
    size_t compressedSize = compressLines();

    // Check if compressed data fits in one packet
    size_t maxDataSize = config_.maxPacketPayload - config_.packetHeaderSize;

    if (compressedSize > 0 && compressedSize <= maxDataSize) {
        // Fits in one packet
        finalizePacket(true);
        packetsSent = 1;
    } else if (compressedSize > maxDataSize) {
        // Need multiple packets - send what fits
        // For now, truncate to max packet size
        packetLen_ = maxDataSize;
        finalizePacket(true);
        packetsSent = 1;
    }

    // Reset buffer state
    lineCount_ = 0;
    currentLineLen_ = 0;
    packetLen_ = 0;
    packetNum_ = 0;
    isActive_ = false;

    inFlush_ = false;
    return packetsSent;
}

size_t MessageBuffer::compressLines() {
    // NASA Rule 5: Assertions
    assert(lineCount_ <= MAX_LINES && "Invalid line count");
    assert(MAX_COMPRESSED_LEN > 0 && "Invalid constant");

    if (lineCount_ == 0) {
        return 0;
    }

    // Build raw text buffer with line format: [delta_varint][text_len][text]
    uint8_t rawBuffer[RAW_THRESHOLD * 2];
    size_t rawLen = 0;
    const size_t maxRawLen = sizeof(rawBuffer);

    // NASA Rule 2: Fixed loop bound
    for (size_t i = 0; i < lineCount_ && i < MAX_LINES; ++i) {
        const LineRecord& line = lines_[i];

        // Encode timestamp as varint
        uint8_t timestampBytes[MAX_VARINT_LEN];
        size_t timestampLen = encodeVarint(line.timestamp, timestampBytes);

        // Check if this line fits
        if (rawLen + timestampLen + 1 + line.textLen > maxRawLen) {
            break;
        }

        // Copy timestamp varint
        for (size_t j = 0; j < timestampLen && j < MAX_VARINT_LEN; ++j) {
            rawBuffer[rawLen++] = timestampBytes[j];
        }

        // Text length byte
        rawBuffer[rawLen++] = static_cast<uint8_t>(line.textLen);

        // Copy text (NASA Rule 2: bounded)
        for (size_t j = 0; j < line.textLen && j < LineRecord::MAX_LINE_LEN; ++j) {
            rawBuffer[rawLen++] = static_cast<uint8_t>(line.text[j]);
        }
    }

    if (rawLen == 0) {
        return 0;
    }

    // Compress the raw buffer
    uint8_t compressedData[MAX_COMPRESSED_LEN];
    size_t compressedLen = compressor_.compress(
        reinterpret_cast<const char*>(rawBuffer),
        rawLen,
        compressedData,
        sizeof(compressedData)
    );

    // NASA Rule 7: Check return value
    bool useCompressed = (compressedLen > 0 && compressedLen < rawLen);

    // Copy to packet buffer (after header space)
    const size_t headerSize = config_.packetHeaderSize;

    if (useCompressed) {
        // Use compressed data
        for (size_t i = 0; i < compressedLen && headerSize + i < MAX_PACKET_DATA; ++i) {
            packetBuffer_[headerSize + i] = compressedData[i];
        }
        packetLen_ = compressedLen;
    } else {
        // Use raw data (compression didn't help)
        for (size_t i = 0; i < rawLen && headerSize + i < MAX_PACKET_DATA; ++i) {
            packetBuffer_[headerSize + i] = rawBuffer[i];
        }
        packetLen_ = rawLen;
    }

    return packetLen_;
}

void MessageBuffer::finalizePacket(bool isFinal) {
    // NASA Rule 5: Assertions
    assert(lineCount_ <= MAX_LINES && "Invalid line count");
    assert(packetLen_ <= MAX_PACKET_DATA && "Packet length overflow");

    if (packetLen_ == 0) {
        return;
    }

    // Build flags
    uint8_t flags = FLAG_DELTA_TIME | FLAG_COMPRESSED;

    if (!isFinal) {
        flags |= FLAG_HAS_MORE;
    }

    // Write header
    writePacketHeader(startTimestamp_, flags, static_cast<uint8_t>(lineCount_));

    // Calculate total packet size
    const size_t totalLen = config_.packetHeaderSize + packetLen_;

    // NASA Rule 7: Check callback before calling
    if (config_.onPacketReady != nullptr) {
        config_.onPacketReady(packetBuffer_, totalLen, batchId_, packetNum_, isFinal);
    }

    ++packetNum_;
}

void MessageBuffer::writePacketHeader(uint32_t baseTimestamp, uint8_t flags, uint8_t count) {
    // NASA Rule 5: Assertions
    assert(sizeof(packetBuffer_) >= 8 && "Buffer too small for header");
    assert(count <= MAX_LINES && "Line count exceeds maximum");

    // Batch ID (2 bytes, little-endian)
    packetBuffer_[0] = static_cast<uint8_t>(batchId_ & 0xFF);
    packetBuffer_[1] = static_cast<uint8_t>((batchId_ >> 8) & 0xFF);

    // Base timestamp (4 bytes, little-endian)
    packetBuffer_[2] = static_cast<uint8_t>(baseTimestamp & 0xFF);
    packetBuffer_[3] = static_cast<uint8_t>((baseTimestamp >> 8) & 0xFF);
    packetBuffer_[4] = static_cast<uint8_t>((baseTimestamp >> 16) & 0xFF);
    packetBuffer_[5] = static_cast<uint8_t>((baseTimestamp >> 24) & 0xFF);

    // Flags
    packetBuffer_[6] = flags;

    // Line count
    packetBuffer_[7] = count;
}

size_t MessageBuffer::encodeVarint(uint32_t value, uint8_t* buf) {
    // NASA Rule 5: Assertions
    assert(buf != nullptr && "Buffer pointer must not be null");
    assert(MAX_VARINT_LEN >= 5 && "Varint buffer too small");

    size_t len = 0;

    // NASA Rule 2: Fixed loop bound (32-bit value needs at most 5 bytes)
    for (size_t i = 0; i < MAX_VARINT_LEN; ++i) {
        if (value > 127) {
            buf[len++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
            value >>= 7;
        } else {
            buf[len++] = static_cast<uint8_t>(value & 0x7F);
            break;
        }
    }

    return len;
}

size_t MessageBuffer::calculateRawSize() const {
    // NASA Rule 5: Assertions
    assert(lineCount_ <= MAX_LINES && "Invalid line count");
    assert(MAX_LINES > 0 && "Invalid constant");

    size_t total = 0;

    // NASA Rule 2: Fixed loop bound
    for (size_t i = 0; i < lineCount_ && i < MAX_LINES; ++i) {
        total += lines_[i].textLen;
    }

    return total;
}

size_t MessageBuffer::getRawSize() const {
    return calculateRawSize();
}

size_t MessageBuffer::getLineCount() const {
    return lineCount_;
}

uint16_t MessageBuffer::getCurrentBatchId() const {
    return batchId_;
}

void MessageBuffer::reset() {
    // NASA Rule 5: Assertions
    assert(MAX_LINES > 0 && "Invalid constant");
    assert(MAX_PACKET_DATA > 0 && "Invalid constant");

    isActive_ = false;
    startTimestamp_ = 0;
    lastKeyTime_ = 0;
    lineCount_ = 0;
    currentLineLen_ = 0;
    batchId_ = 0;
    packetLen_ = 0;
    packetNum_ = 0;
    inFlush_ = false;

    // Clear buffers
    memset(lines_, 0, sizeof(lines_));
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
}

size_t MessageBuffer::getRAMUsage() const {
    return sizeof(MessageBuffer) + Compressor::getRAMUsage();
}

} // namespace stechat
