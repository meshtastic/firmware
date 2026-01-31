#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "AirQualityDatabase.h"
#include "RTC.h"
#include <cmath>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

// Check for filesystem support
#ifdef FSCom
#include "FS.h"
#endif

/**
 * Convert DatabaseRecord to protobuf TelemetryDatabaseRecord
 */
meshtastic_TelemetryDatabaseRecord AirQualityDatabase::recordToProtobuf(const DatabaseRecord &record) const
{
    meshtastic_TelemetryDatabaseRecord pb = {};

    pb.timestamp = record.timestamp;
    pb.flags.delivered_to_mesh = record.flags.delivered_to_mesh;

    // Set the air quality metrics in the oneof field
    pb.which_telemetry_data = meshtastic_TelemetryDatabaseRecord_air_quality_metrics_tag;
    pb.telemetry_data.air_quality_metrics = record.telemetry;

    return pb;
}

/**
 * Convert protobuf TelemetryDatabaseRecord to DatabaseRecord
 */
AirQualityDatabase::DatabaseRecord AirQualityDatabase::recordFromProtobuf(const meshtastic_TelemetryDatabaseRecord &pb) const
{
    DatabaseRecord record = {};

    record.timestamp = pb.timestamp;
    record.flags.delivered_to_mesh = pb.flags.delivered_to_mesh;

    // Extract air quality metrics from oneof field
    if (pb.which_telemetry_data == meshtastic_TelemetryDatabaseRecord_air_quality_metrics_tag) {
        record.telemetry = pb.telemetry_data.air_quality_metrics;
    }

    return record;
}

AirQualityDatabase::AirQualityDatabase()
{
    // Initialize with empty records
}

bool AirQualityDatabase::init()
{
    concurrency::Lock lock(recordsLock);
    records.clear();
    LOG_DEBUG("AirQualityDatabase: Initialized");
    return loadFromStorage();
}

bool AirQualityDatabase::addRecord(const DatabaseRecord &record)
{
    concurrency::Lock lock(recordsLock);

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
    concurrency::Lock lock(recordsLock);

    if (index >= records.size()) {
        return false;
    }

    record = records[index];
    return true;
}

std::vector<DatabaseRecord> AirQualityDatabase::getAllRecords() const
{
    concurrency::Lock lock(recordsLock);

    std::vector<DatabaseRecord> result(records.begin(), records.end());
    return result;
}

bool AirQualityDatabase::markDeliveredToMesh(uint32_t index)
{
    concurrency::Lock lock(recordsLock);

    if (index >= records.size()) {
        return false;
    }

    records[index].flags.delivered_to_mesh = true;
    return saveToStorage();
}

bool AirQualityDatabase::markAllDeliveredToMesh()
{
    concurrency::Lock lock(recordsLock);

    for (auto &record : records) {
        record.flags.delivered_to_mesh = true;
    }

    return saveToStorage();
}

uint32_t AirQualityDatabase::getRecordCount() const
{
    concurrency::Lock lock(recordsLock);
    return records.size();
}

bool AirQualityDatabase::clearAll()
{
    concurrency::Lock lock(recordsLock);
    records.clear();
    LOG_DEBUG("AirQualityDatabase: Cleared all records");
    return saveToStorage();
}

bool AirQualityDatabase::loadFromStorage()
{
#ifdef FSCom
    concurrency::Lock lock(recordsLock);

    try {
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

        if (bytesRead != fileSize) {
            LOG_ERROR("AirQualityDatabase: Failed to read complete database (read %zu of %zu bytes)", bytesRead, fileSize);
            return false;
        }

        // Create input stream with known buffer size
        pb_istream_t stream = pb_istream_from_buffer(buffer.data(), fileSize);
        meshtastic_TelemetryDatabaseSnapshot snapshot = {};

        // Decode the protobuf message
        if (!pb_decode(&stream, meshtastic_TelemetryDatabaseSnapshot_fields, &snapshot)) {
            LOG_ERROR("AirQualityDatabase: Failed to decode snapshot: %s", PB_GET_ERROR(&stream));
            return false;
        }

        // Clear existing records and load from snapshot
        records.clear();
        for (uint32_t i = 0; i < snapshot.records_count; i++) {
            DatabaseRecord record = recordFromProtobuf(snapshot.records[i]);
            records.push_back(record);
        }

        LOG_DEBUG("AirQualityDatabase: Loaded %d records from storage", records.size());
        return true;

    } catch (const std::exception &e) {
        LOG_ERROR("AirQualityDatabase: Exception loading from storage: %s", e.what());
        return false;
    }

#else
    LOG_DEBUG("AirQualityDatabase: FSCom not available, skipping storage load");
    return true;
#endif
}

bool AirQualityDatabase::saveToStorage()
{
#ifdef FSCom
    concurrency::Lock lock(recordsLock);

    try {
        // Create snapshot from current records
        meshtastic_TelemetryDatabaseSnapshot snapshot = {};
        snapshot.snapshot_timestamp = getUnixTime(); // Current time
        snapshot.version = 1;
        snapshot.records_count = records.size();

        // Check if we have too many records for fixed array
        if (snapshot.records_count > 100) { // Sanity check
            LOG_WARN("AirQualityDatabase: Too many records (%d), truncating to 100", snapshot.records_count);
            snapshot.records_count = 100;
        }

        // Convert each record to protobuf format
        for (uint32_t i = 0; i < snapshot.records_count; i++) {
            snapshot.records[i] = recordToProtobuf(records[i]);
        }

        // Encode to buffer first to determine size
        uint8_t buffer[65536]; // Max buffer size
        pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));

        if (!pb_encode(&ostream, meshtastic_TelemetryDatabaseSnapshot_fields, &snapshot)) {
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

        LOG_DEBUG("AirQualityDatabase: Saved %d records to storage (%zu bytes)", records.size(), encodedSize);
        return true;

    } catch (const std::exception &e) {
        LOG_ERROR("AirQualityDatabase: Exception saving to storage: %s", e.what());
        return false;
    }

#else
    LOG_DEBUG("AirQualityDatabase: FSCom not available, skipping storage save");
    return true;
#endif
}

TelemetryDatabase<meshtastic_AirQualityMetrics>::Statistics AirQualityDatabase::getStatistics() const
{
    concurrency::Lock lock(recordsLock);
    Statistics stats = {};

    if (records.empty()) {
        return stats;
    }

    stats.record_count = records.size();
    stats.min_timestamp = records.front().timestamp;
    stats.max_timestamp = records.back().timestamp;

    for (const auto &record : records) {
        if (record.flags.delivered_to_mesh) {
            stats.delivered_mesh++;
        }
    }

    return stats;
}

#endif
