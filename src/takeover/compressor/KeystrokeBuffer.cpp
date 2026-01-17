/**
 * @file KeystrokeBuffer.cpp
 * @brief Circular buffer implementation for keystroke compression
 */

#include "KeystrokeBuffer.h"
#include <string.h>

namespace takeover {

KeystrokeBuffer::KeystrokeBuffer()
    : recordHead_(0)
    , recordTail_(0)
    , recordCount_(0)
    , batchId_(0)
    , batchStartTime_(0)
    , lastKeystrokeTime_(0)
    , packetLen_(0)
    , packetRecordCount_(0)
    , packetNum_(0)
{
    memset(records_, 0, sizeof(records_));
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
}

void KeystrokeBuffer::setConfig(const KeystrokeBufferConfig& config) {
    config_ = config;
}

bool KeystrokeBuffer::addKeystroke(const char* text, uint32_t timestampMs) {
    if (!text) return false;
    return addKeystroke(text, strlen(text), timestampMs);
}

bool KeystrokeBuffer::addKeystroke(const char* text, size_t textLen, uint32_t timestampMs) {
    if (!text || textLen == 0) return false;
    if (textLen > sizeof(records_[0].text) - 1) {
        textLen = sizeof(records_[0].text) - 1;
    }

    // Use provided timestamp or current time
    uint32_t now = timestampMs;

    // Check if this is the first record in batch
    bool isFirst = (recordCount_ == 0);

    if (isFirst) {
        batchStartTime_ = now;
        batchId_++;
        packetLen_ = 0;
        packetRecordCount_ = 0;
        packetNum_ = 0;
    }

    // Check if buffer is full
    if (recordCount_ >= MAX_RECORDS) {
        // Flush oldest records
        flush();
    }

    // Add record to circular buffer
    KeystrokeRecord& record = records_[recordHead_];
    record.isAbsolute = isFirst;

    if (isFirst) {
        // Store absolute timestamp (Unix seconds)
        record.timestamp = now / 1000;  // Convert to seconds
    } else {
        // Store delta from last keystroke (ms)
        record.timestamp = now - lastKeystrokeTime_;
    }

    memcpy(record.text, text, textLen);
    record.text[textLen] = '\0';
    record.textLen = textLen;

    // Update circular buffer pointers
    recordHead_ = (recordHead_ + 1) % MAX_RECORDS;
    if (recordCount_ < MAX_RECORDS) {
        recordCount_++;
    } else {
        // Overwrite oldest - move tail
        recordTail_ = (recordTail_ + 1) % MAX_RECORDS;
    }

    lastKeystrokeTime_ = now;

    // Try to compress and add to packet
    compressAndAddToPacket(record);

    // Check if we should auto-flush
    if (recordCount_ >= config_.maxRecordsPerBatch) {
        flush();
    }

    return true;
}

bool KeystrokeBuffer::compressAndAddToPacket(const KeystrokeRecord& record) {
    // Calculate available space in packet
    size_t maxDataSize = config_.maxPacketPayload - config_.packetHeaderSize;

    // Compress the text
    uint8_t compressedText[128];
    size_t compressedLen = compressor_.compress(record.text, record.textLen,
                                                 compressedText, sizeof(compressedText));

    if (compressedLen == 0) {
        // Compression failed, use raw text
        compressedLen = record.textLen;
        memcpy(compressedText, record.text, compressedLen);
    }

    // Encode delta timestamp as varint
    uint8_t deltaBytes[5];
    size_t deltaLen = encodeVarint(record.timestamp, deltaBytes);

    // Record format: [delta_varint][compressed_len][compressed_data]
    size_t recordSize = deltaLen + 1 + compressedLen;

    // Check if record fits in current packet
    if (packetLen_ + recordSize > maxDataSize) {
        // Finalize current packet and start new one
        if (packetRecordCount_ > 0) {
            finalizePacket(false);  // More packets to come
        }

        // Reset packet buffer
        packetLen_ = 0;
        packetRecordCount_ = 0;
        packetNum_++;
    }

    // Add record to packet
    if (packetLen_ + recordSize <= maxDataSize) {
        // Delta timestamp
        memcpy(packetBuffer_ + config_.packetHeaderSize + packetLen_, deltaBytes, deltaLen);
        packetLen_ += deltaLen;

        // Compressed length
        packetBuffer_[config_.packetHeaderSize + packetLen_] = compressedLen;
        packetLen_++;

        // Compressed data
        memcpy(packetBuffer_ + config_.packetHeaderSize + packetLen_, compressedText, compressedLen);
        packetLen_ += compressedLen;

        packetRecordCount_++;
        return true;
    }

    return false;
}

void KeystrokeBuffer::finalizePacket(bool isFinal) {
    if (packetRecordCount_ == 0) return;

    // Build flags
    uint8_t flags = FLAG_COMPRESSED | FLAG_DELTA_TIME;
    if (!isFinal) {
        flags |= FLAG_HAS_MORE;
    }

    // Write header
    writePacketHeader(batchStartTime_ / 1000, flags, packetRecordCount_);

    // Calculate total packet size
    size_t totalLen = config_.packetHeaderSize + packetLen_;

    // Call callback
    if (config_.onPacketReady) {
        config_.onPacketReady(packetBuffer_, totalLen, batchId_, packetNum_, isFinal);
    }
}

void KeystrokeBuffer::writePacketHeader(uint32_t baseTimestamp, uint8_t flags, uint8_t count) {
    // Batch ID (2 bytes, little-endian)
    packetBuffer_[0] = batchId_ & 0xFF;
    packetBuffer_[1] = (batchId_ >> 8) & 0xFF;

    // Base timestamp (4 bytes, little-endian)
    packetBuffer_[2] = baseTimestamp & 0xFF;
    packetBuffer_[3] = (baseTimestamp >> 8) & 0xFF;
    packetBuffer_[4] = (baseTimestamp >> 16) & 0xFF;
    packetBuffer_[5] = (baseTimestamp >> 24) & 0xFF;

    // Flags
    packetBuffer_[6] = flags;

    // Record count
    packetBuffer_[7] = count;
}

uint8_t KeystrokeBuffer::flush() {
    uint8_t packetsSent = 0;

    // Finalize current packet if there's data
    if (packetRecordCount_ > 0) {
        finalizePacket(true);
        packetsSent = packetNum_ + 1;
    }

    // Clear buffer
    recordHead_ = 0;
    recordTail_ = 0;
    recordCount_ = 0;
    packetLen_ = 0;
    packetRecordCount_ = 0;
    packetNum_ = 0;
    batchStartTime_ = 0;

    return packetsSent;
}

bool KeystrokeBuffer::needsFlush() const {
    if (recordCount_ == 0) return false;

    // Check timeout
    // Note: In real implementation, compare with current time
    // For now, just check if we have records

    // Check max records
    if (recordCount_ >= config_.maxRecordsPerBatch) return true;

    return false;
}

void KeystrokeBuffer::reset() {
    recordHead_ = 0;
    recordTail_ = 0;
    recordCount_ = 0;
    batchId_ = 0;
    batchStartTime_ = 0;
    lastKeystrokeTime_ = 0;
    packetLen_ = 0;
    packetRecordCount_ = 0;
    packetNum_ = 0;
    memset(packetBuffer_, 0, sizeof(packetBuffer_));
}

size_t KeystrokeBuffer::getRAMUsage() const {
    return sizeof(KeystrokeBuffer) + Unishox2::getRAMUsage();
}

size_t KeystrokeBuffer::encodeVarint(uint32_t value, uint8_t* buf) {
    size_t len = 0;
    while (value > 127) {
        buf[len++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    buf[len++] = value & 0x7F;
    return len;
}

uint32_t KeystrokeBuffer::decodeVarint(const uint8_t* buf, size_t* bytesRead) {
    uint32_t value = 0;
    size_t shift = 0;
    size_t len = 0;

    while (true) {
        uint8_t byte = buf[len++];
        value |= (uint32_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
        if (shift >= 35) break; // Overflow protection
    }

    if (bytesRead) *bytesRead = len;
    return value;
}

} // namespace takeover
