#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "AirQualityDatabase.h"
#include "RTC.h"
#include <cmath>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

// Check for filesystem support
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"

/**
 * Convert DatabaseRecord to protobuf TelemetryDatabaseRecord
 */
meshtastic_TelemetryDatabaseRecord AirQualityDatabase::recordToProtobuf(const DatabaseRecord &record) const
{
    meshtastic_TelemetryDatabaseRecord pb = {};

    pb.delivered = record.delivered;
    pb.telemetry = record.telemetry;

    return pb;
}

/**
 * Convert protobuf TelemetryDatabaseRecord to DatabaseRecord
 */
DatabaseRecord AirQualityDatabase::recordFromProtobuf(const meshtastic_TelemetryDatabaseRecord &pb) const
{
    DatabaseRecord record = {};

    record.delivered = pb.delivered;
    record.telemetry = pb.telemetry;

    return record;
}

AirQualityDatabase::AirQualityDatabase()
{
    // Initialize with empty records
}

bool AirQualityDatabase::init()
{
    concurrency::LockGuard g(recordsLock);
    records.clear();
    LOG_DEBUG("AirQualityDatabase: Initialized");
    return loadFromStorage();
}

bool AirQualityDatabase::addRecord(const DatabaseRecord &record)
{
    concurrency::LockGuard g(recordsLock);

    // If at capacity, remove oldest record
    if (records.size() >= MAX_RECORDS) {
        LOG_DEBUG("AirQualityDatabase: At capacity (%d), removing oldest record", MAX_RECORDS);
        records.pop_front();
    }

    records.push_back(record);
    LOG_DEBUG("AirQualityDatabase: Added record (total: %d)", records.size());

    // Save to storage after each addition (with protobuf serialization)
    return saveToStorage();
}

bool AirQualityDatabase::getRecord(uint32_t index, DatabaseRecord &record) const
{
    concurrency::LockGuard g(recordsLock);

    if (index >= records.size()) {
        return false;
    }

    record = records[index];
    return true;
}

std::vector<DatabaseRecord> AirQualityDatabase::getAllRecords() const
{
    concurrency::LockGuard g(recordsLock);

    std::vector<DatabaseRecord> result(records.begin(), records.end());
    return result;
}

bool AirQualityDatabase::markDelivered(uint32_t index)
{
    concurrency::LockGuard g(recordsLock);

    if (index >= records.size()) {
        return false;
    }

    records[index].delivered = true;
    return saveToStorage();
}

bool AirQualityDatabase::markAllDelivered()
{
    concurrency::LockGuard g(recordsLock);

    for (auto &record : records) {
        record.delivered = true;
    }
    return saveToStorage();
}

uint32_t AirQualityDatabase::getRecordCount() const
{
    return records.size();
}

bool AirQualityDatabase::clearAll()
{
    concurrency::LockGuard g(recordsLock);
    records.clear();
    LOG_DEBUG("AirQualityDatabase: Cleared all records");
    return saveToStorage();
}

bool AirQualityDatabase::loadFromStorage()
{
#ifdef FSCom
    concurrency::LockGuard g(recordsLock);
    spiLock->lock();

    // Try to open the database file
    File dbFile = FSCom.open(STORAGE_KEY, FILE_O_READ);
    if (!dbFile) {
        LOG_DEBUG("AirQualityDatabase: No saved database found (first boot)");
        return true; // OK on first boot
    }

    // Get file size to allocate buffer
    size_t fileSize = dbFile.size();
    if (fileSize == 0 || fileSize > 65536) { // Sanity check: max 64KB
        LOG_WARN("AirQualityDatabase: Invalid database size: %zu bytes", fileSize);
        dbFile.close();
        return false;
    }

    // Allocate buffer for the entire file
    std::vector<uint8_t> buffer(fileSize);

    // Read entire file into buffer
    size_t bytesRead = dbFile.read(buffer.data(), fileSize);
    dbFile.close();
    spiLock->unlock();

    if (bytesRead != fileSize) {
        LOG_ERROR("AirQualityDatabase: Failed to read complete database (read %zu of %zu bytes)", bytesRead, fileSize);
        return false;
    }

    // Create input stream with known buffer size
    pb_istream_t stream = pb_istream_from_buffer(buffer.data(), fileSize);
    meshtastic_TelemetryDatabase snapshot = meshtastic_TelemetryDatabase_init_zero;

    // Decode the protobuf message
    if (!pb_decode(&stream, meshtastic_TelemetryDatabase_fields, &snapshot)) {
        LOG_ERROR("AirQualityDatabase: Failed to decode snapshot: %s", PB_GET_ERROR(&stream));
        return false;
    }

    // Clear existing records and load from snapshot
    records.clear();
    for (uint32_t i = 0; i < snapshot.record_count; i++) {
        DatabaseRecord record = recordFromProtobuf(snapshot.records[i]);
        records.push_back(record);
    }
    LOG_DEBUG("AirQualityDatabase: Loaded %d records from storage", records.size());
    return true;

#else
    LOG_DEBUG("AirQualityDatabase: FSCom not available, skipping storage load");
    return true;
#endif
}

bool AirQualityDatabase::saveToStorage()
{
#ifdef FSCom
    concurrency::LockGuard g(recordsLock);
    spiLock->lock();

    // Create snapshot from current records
    meshtastic_TelemetryDatabase snapshot = meshtastic_TelemetryDatabase_init_zero;
    snapshot.record_count = records.size();

    // Check if we have too many records for fixed array
    if (snapshot.record_count > 100) { // Sanity check
        LOG_WARN("AirQualityDatabase: Too many records (%d), truncating to 100", snapshot.record_count);
        snapshot.record_count = 100;
    }

    // Convert each record to protobuf format
    for (uint32_t i = 0; i < snapshot.record_count; i++) {
        snapshot.records[i] = recordToProtobuf(records[i]);
    }

    // Encode to buffer first to determine size
    uint8_t buffer[65536]; // Max buffer size
    pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&ostream, meshtastic_TelemetryDatabase_fields, &snapshot)) {
        LOG_ERROR("AirQualityDatabase: Failed to encode snapshot: %s", PB_GET_ERROR(&ostream));
        return false;
    }

    size_t encodedSize = ostream.bytes_written;

    // Write to LittleFS file
    File dbFile = FSCom.open(STORAGE_KEY, FILE_O_WRITE);
    if (!dbFile) {
        LOG_ERROR("AirQualityDatabase: Failed to open file for writing");
        return false;
    }

    size_t bytesWritten = dbFile.write(buffer, encodedSize);
    dbFile.close();

    if (bytesWritten != encodedSize) {
        LOG_ERROR("AirQualityDatabase: Failed to write complete database (wrote %zu of %zu bytes)", bytesWritten,
                    encodedSize);
        // Try to remove incomplete file
        FSCom.remove(STORAGE_KEY);
        return false;
    }
    spiLock->unlock();
    LOG_DEBUG("AirQualityDatabase: Saved %d records to storage (%zu bytes)", records.size(), encodedSize);
    return true;
#else
    LOG_DEBUG("AirQualityDatabase: FSCom not available, skipping storage save");
    return true;
#endif
}

TelemetryDatabase<meshtastic_AirQualityMetrics>::Statistics AirQualityDatabase::getStatistics() const
{
    concurrency::LockGuard g(recordsLock);
    Statistics stats = {};

    if (records.empty()) {
        return stats;
    }

    stats.record_count = records.size();
    stats.min_timestamp = records.front().telemetry.time;
    stats.max_timestamp = records.back().telemetry.time;

    for (const auto &record : records) {
        if (record.delivered) {
            stats.delivered++;
        }
    }
    return stats;
}

std::vector<DatabaseRecord> AirQualityDatabase::getRecordsForRecovery() const
{
    concurrency::LockGuard g(recordsLock);
    std::vector<DatabaseRecord> recoveryRecords;

    for (const auto &record : records) {
        if (!record.delivered) {
            recoveryRecords.push_back(record);
        }
    }

    LOG_DEBUG("AirQualityDatabase: Found %d records for recovery", recoveryRecords.size());
    return recoveryRecords;
}
#endif
