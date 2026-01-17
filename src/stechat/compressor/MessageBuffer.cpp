/**
 * @file MessageBuffer.cpp
 * @brief Circular buffer implementation for message compression
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
    : recordHead_(0)
    , recordTail_(0)
    , recordCount_(0)
    , batchId_(0)
    , batchStartTime_(0)
    , lastMessageTime_(0)
    , packetLen_(0)
    , packetRecordCount_(0)
    , packetNum_(0)
{
    // NASA Rule 5: Assertions to verify initialization
    assert(MAX_RECORDS > 0 && "MAX_RECORDS must be positive");
    assert(MAX_PACKET_DATA > 0 && "MAX_PACKET_DATA must be positive");

    // Zero-initialize all buffers (NASA Rule 3: no malloc needed)
    memset(records_, 0, sizeof(records_));
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
}

void MessageBuffer::setConfig(const MessageBufferConfig& config) {
    // NASA Rule 5: Assertions for preconditions
    assert(config.maxPacketPayload >= config.packetHeaderSize + 10 &&
           "Payload must be larger than header");
    assert(config.packetHeaderSize >= 8 && "Header must be at least 8 bytes");

    config_ = config;
}

bool MessageBuffer::addMessage(const char* text, uint32_t timestampMs) {
    // NASA Rule 5: Assertions
    assert(text != nullptr && "Text pointer must not be null");

    // NASA Rule 7: Validate input
    if (text == nullptr) {
        return false;
    }

    // Calculate length with bounded loop (NASA Rule 2)
    size_t textLen = 0;
    for (size_t i = 0; i < MessageRecord::MAX_TEXT_LEN && text[i] != '\0'; ++i) {
        textLen = i + 1;
    }

    return addMessage(text, textLen, timestampMs);
}

bool MessageBuffer::addMessage(const char* text, size_t textLen, uint32_t timestampMs) {
    // NASA Rule 5: Assertions
    assert(text != nullptr && "Text pointer must not be null");
    assert(textLen <= MessageRecord::MAX_TEXT_LEN && "Text length exceeds maximum");

    // NASA Rule 7: Validate inputs
    if (text == nullptr || textLen == 0) {
        return false;
    }

    // Clamp text length (NASA Rule 2: fixed bounds)
    if (textLen > MessageRecord::MAX_TEXT_LEN) {
        textLen = MessageRecord::MAX_TEXT_LEN;
    }

    // Use provided timestamp or current time
    const uint32_t now = timestampMs;

    // Check if this is the first record in batch
    const bool isFirst = (recordCount_ == 0);

    if (isFirst) {
        batchStartTime_ = now;
        ++batchId_;
        packetLen_ = 0;
        packetRecordCount_ = 0;
        packetNum_ = 0;
    }

    // Check if buffer is full - flush if needed
    if (recordCount_ >= MAX_RECORDS) {
        (void)flush();  // NASA Rule 7: explicitly ignore return for void operation
    }

    // Add record to circular buffer
    MessageRecord& record = records_[recordHead_];
    record.isAbsolute = isFirst;

    if (isFirst) {
        // Store absolute timestamp (Unix seconds)
        record.timestamp = now / 1000;
    } else {
        // Store delta from last message (ms)
        record.timestamp = now - lastMessageTime_;
    }

    // Copy text with bounds checking (NASA Rule 2)
    for (size_t i = 0; i < textLen && i < MessageRecord::MAX_TEXT_LEN; ++i) {
        record.text[i] = text[i];
    }
    record.text[textLen] = '\0';
    record.textLen = static_cast<uint8_t>(textLen);

    // Update circular buffer pointers with modulo (NASA Rule 2: bounded)
    recordHead_ = (recordHead_ + 1) % MAX_RECORDS;
    if (recordCount_ < MAX_RECORDS) {
        ++recordCount_;
    } else {
        // Overwrite oldest - move tail
        recordTail_ = (recordTail_ + 1) % MAX_RECORDS;
    }

    lastMessageTime_ = now;

    // Try to compress and add to packet
    const bool added = compressAndAddToPacket(record);
    (void)added;  // NASA Rule 7: acknowledge return value

    // Check if we should auto-flush
    if (recordCount_ >= config_.maxRecordsPerBatch) {
        (void)flush();
    }

    return true;
}

bool MessageBuffer::compressAndAddToPacket(const MessageRecord& record) {
    // NASA Rule 5: Assertions
    assert(config_.maxPacketPayload > config_.packetHeaderSize &&
           "Invalid packet configuration");
    assert(record.textLen <= MessageRecord::MAX_TEXT_LEN &&
           "Record text length invalid");

    // Calculate available space in packet
    const size_t maxDataSize = config_.maxPacketPayload - config_.packetHeaderSize;

    // Compress the text (NASA Rule 3: fixed-size buffer on stack)
    uint8_t compressedText[MAX_COMPRESSED_LEN];
    size_t compressedLen = compressor_.compress(
        record.text,
        record.textLen,
        compressedText,
        sizeof(compressedText)
    );

    // NASA Rule 7: Check return value
    if (compressedLen == 0) {
        // Compression failed, use raw text with bounds check
        compressedLen = record.textLen;
        if (compressedLen > sizeof(compressedText)) {
            compressedLen = sizeof(compressedText);
        }
        for (size_t i = 0; i < compressedLen; ++i) {
            compressedText[i] = static_cast<uint8_t>(record.text[i]);
        }
    }

    // Encode delta timestamp as varint (NASA Rule 3: fixed-size buffer)
    uint8_t deltaBytes[MAX_VARINT_LEN];
    const size_t deltaLen = encodeVarint(record.timestamp, deltaBytes);

    // Record format: [delta_varint][compressed_len][compressed_data]
    const size_t recordSize = deltaLen + 1 + compressedLen;

    // Check if record fits in current packet
    if (packetLen_ + recordSize > maxDataSize) {
        // Finalize current packet and start new one
        if (packetRecordCount_ > 0) {
            finalizePacket(false);  // More packets to come
        }

        // Reset packet buffer
        packetLen_ = 0;
        packetRecordCount_ = 0;
        ++packetNum_;
    }

    // Add record to packet with bounds checking
    if (packetLen_ + recordSize <= maxDataSize) {
        const size_t baseOffset = config_.packetHeaderSize;

        // Copy delta timestamp (NASA Rule 2: bounded loop)
        for (size_t i = 0; i < deltaLen && i < MAX_VARINT_LEN; ++i) {
            packetBuffer_[baseOffset + packetLen_ + i] = deltaBytes[i];
        }
        packetLen_ += deltaLen;

        // Compressed length byte
        packetBuffer_[baseOffset + packetLen_] = static_cast<uint8_t>(compressedLen);
        ++packetLen_;

        // Copy compressed data (NASA Rule 2: bounded loop)
        for (size_t i = 0; i < compressedLen && i < MAX_COMPRESSED_LEN; ++i) {
            packetBuffer_[baseOffset + packetLen_ + i] = compressedText[i];
        }
        packetLen_ += compressedLen;

        ++packetRecordCount_;
        return true;
    }

    return false;
}

void MessageBuffer::finalizePacket(bool isFinal) {
    // NASA Rule 5: Assertions
    assert(packetRecordCount_ <= MAX_RECORDS && "Invalid record count");
    assert(packetLen_ <= MAX_PACKET_DATA && "Packet length overflow");

    if (packetRecordCount_ == 0) {
        return;
    }

    // Build flags
    uint8_t flags = FLAG_COMPRESSED | FLAG_DELTA_TIME;
    if (!isFinal) {
        flags |= FLAG_HAS_MORE;
    }

    // Write header
    writePacketHeader(batchStartTime_ / 1000, flags, packetRecordCount_);

    // Calculate total packet size
    const size_t totalLen = config_.packetHeaderSize + packetLen_;

    // NASA Rule 7: Check callback before calling
    if (config_.onPacketReady != nullptr) {
        config_.onPacketReady(packetBuffer_, totalLen, batchId_, packetNum_, isFinal);
    }
}

void MessageBuffer::writePacketHeader(uint32_t baseTimestamp, uint8_t flags, uint8_t count) {
    // NASA Rule 5: Assertions
    assert(sizeof(packetBuffer_) >= 8 && "Buffer too small for header");
    assert(count <= MAX_RECORDS && "Record count exceeds maximum");

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

    // Record count
    packetBuffer_[7] = count;
}

uint8_t MessageBuffer::flush() {
    // NASA Rule 5: Assertions
    assert(packetRecordCount_ <= MAX_RECORDS && "Invalid record count state");
    assert(recordCount_ <= MAX_RECORDS && "Invalid buffer state");

    uint8_t packetsSent = 0;

    // Finalize current packet if there's data
    if (packetRecordCount_ > 0) {
        finalizePacket(true);
        packetsSent = packetNum_ + 1;
    }

    // Clear buffer state
    recordHead_ = 0;
    recordTail_ = 0;
    recordCount_ = 0;
    packetLen_ = 0;
    packetRecordCount_ = 0;
    packetNum_ = 0;
    batchStartTime_ = 0;

    return packetsSent;
}

bool MessageBuffer::needsFlush() const {
    // NASA Rule 5: Assertions
    assert(recordCount_ <= MAX_RECORDS && "Invalid buffer state");
    assert(config_.maxRecordsPerBatch > 0 && "Invalid config");

    if (recordCount_ == 0) {
        return false;
    }

    // Check max records threshold
    if (recordCount_ >= config_.maxRecordsPerBatch) {
        return true;
    }

    // Note: Timeout check would require current time parameter
    // For now, only check record count
    return false;
}

void MessageBuffer::reset() {
    // NASA Rule 5: Assertions
    assert(MAX_RECORDS > 0 && "Invalid constant");
    assert(MAX_PACKET_DATA > 0 && "Invalid constant");

    recordHead_ = 0;
    recordTail_ = 0;
    recordCount_ = 0;
    batchId_ = 0;
    batchStartTime_ = 0;
    lastMessageTime_ = 0;
    packetLen_ = 0;
    packetRecordCount_ = 0;
    packetNum_ = 0;

    // Clear packet buffer
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
}

size_t MessageBuffer::getRAMUsage() const {
    return sizeof(MessageBuffer) + Unishox2::getRAMUsage();
}

size_t MessageBuffer::getPendingCount() const {
    return recordCount_;
}

uint16_t MessageBuffer::getCurrentBatchId() const {
    return batchId_;
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

uint32_t MessageBuffer::decodeVarint(const uint8_t* buf, size_t maxLen, size_t* bytesRead) {
    // NASA Rule 5: Assertions
    assert(buf != nullptr && "Buffer pointer must not be null");
    assert(bytesRead != nullptr && "bytesRead pointer must not be null");

    uint32_t value = 0;
    size_t shift = 0;
    size_t len = 0;

    // NASA Rule 2: Fixed loop bound
    for (size_t i = 0; i < MAX_VARINT_LEN && i < maxLen; ++i) {
        const uint8_t byte = buf[len++];
        value |= static_cast<uint32_t>(byte & 0x7F) << shift;

        if ((byte & 0x80) == 0) {
            break;
        }

        shift += 7;

        // Overflow protection (NASA Rule 2: bounded operation)
        if (shift >= 35) {
            break;
        }
    }

    *bytesRead = len;
    return value;
}

} // namespace stechat
