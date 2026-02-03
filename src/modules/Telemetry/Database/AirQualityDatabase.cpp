#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && !MESHTASTIC_EXCLUDE_AIR_QUALITY_DB

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

AirQualityDatabase::AirQualityDatabase()
{
}

bool AirQualityDatabase::init()
{
    records.clear();
    return loadFromStorage();
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

bool AirQualityDatabase::addRecord(const DatabaseRecord &record)
{
    // recordsLock->lock();

    // If at capacity, remove oldest record
    if (records.size() >= MAX_RECORDS) {
        LOG_DEBUG("AirQualityDatabase: At capacity (%d), removing oldest record", MAX_RECORDS);
        records.pop_front();
    }

    records.push_back(record);
    // recordsLock->unlock();
    LOG_DEBUG("AirQualityDatabase: Added record (total: %d)", records.size());
    // Save to storage after each addition (with protobuf serialization)
    return saveToStorage();
}

bool AirQualityDatabase::getRecord(uint32_t index, DatabaseRecord &record) const
{
    if (index >= records.size()) {
        return false;
    }

    record = records[index];
    return true;
}

std::vector<DatabaseRecord> AirQualityDatabase::getAllRecords() const
{
    std::vector<DatabaseRecord> result(records.begin(), records.end());
    return result;
}

bool AirQualityDatabase::markDelivered(uint32_t index)
{

    if (index >= records.size()) {
        return false;
    }
    records[index].delivered = true;
    return saveToStorage();
}

bool AirQualityDatabase::markAllDelivered()
{
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
    // TODO - needs the lock?
    // recordsLock->lock();
    records.clear();
    // recordsLock->unlock();
    LOG_DEBUG("AirQualityDatabase: Cleared all records");
    return saveToStorage();
}

bool AirQualityDatabase::loadFromStorage()
{
#ifdef FSCom
    spiLock->lock();
    meshtastic_TelemetryDatabase meshtastic_telemetry_database = meshtastic_TelemetryDatabase_init_zero;
    // Try to open the database file
    auto file = FSCom.open(STORAGE_KEY, FILE_O_READ);
    if (!file) {
        spiLock->unlock();
        LOG_DEBUG("AirQualityDatabase: No saved database found (first boot)");
        return true; // OK on first boot
    }

    // Get file size to allocate buffer
    size_t fileSize = file.size();
    if (fileSize == 0 || fileSize > 65536) { // Sanity check: max 64KB
        LOG_WARN("AirQualityDatabase: Invalid database size: %zu bytes", fileSize);
        file.close();
        spiLock->unlock();
        return false;
    }

    // Allocate buffer for the entire file
    std::vector<uint8_t> buffer(fileSize);

    // Read entire file into buffer
    size_t bytesRead = file.read(buffer.data(), fileSize);
    file.close();

    if (bytesRead != fileSize) {
        LOG_ERROR("AirQualityDatabase: Failed to read complete database (read %zu of %zu bytes)", bytesRead, fileSize);
        spiLock->unlock();
        return false;
    }

    // Create input stream with known buffer size
    pb_istream_t stream = pb_istream_from_buffer(buffer.data(), fileSize);

    // Decode the protobuf message
    if (!pb_decode(&stream, meshtastic_TelemetryDatabase_fields, &meshtastic_telemetry_database)) {
        LOG_ERROR("AirQualityDatabase: Failed to decode snapshot: %s", PB_GET_ERROR(&stream));
        spiLock->unlock();
        return false;
    }

    // Clear existing records and load from snapshot
    records.clear();
    for (uint32_t i = 0; i < meshtastic_telemetry_database.records_count; i++) {
        DatabaseRecord record = recordFromProtobuf(meshtastic_telemetry_database.records[i]);
        records.push_back(record);
    }
    LOG_DEBUG("AirQualityDatabase: Loaded %d records from storage", records.size());
    spiLock->unlock();
    return true;
#else
    LOG_DEBUG("AirQualityDatabase: FSCom not available, skipping storage load");
    return true;
#endif
}

bool AirQualityDatabase::saveToStorage()
{
#ifdef FSCom
    LOG_DEBUG("AirQualityDatabase: Attempting save to storage");
    bool okay = false;
    auto file = SafeFile(STORAGE_KEY);
    size_t encodedSize = 0;
    meshtastic_TelemetryDatabase meshtastic_telemetry_database = meshtastic_TelemetryDatabase_init_zero;

    // Check if we have too many records for fixed array
    if (records.size() > MAX_RECORDS) { // Sanity check
        LOG_WARN("AirQualityDatabase: Too many records (%d), truncating to %d", records.size(), MAX_RECORDS);
        meshtastic_telemetry_database.records_count = MAX_RECORDS;
    } else {
        meshtastic_telemetry_database.records_count = records.size();
    }

    LOG_DEBUG("AirQualityDatabase: Converting record to protobuf");
    // Convert each record to protobuf format
    for (uint32_t i = 0; i < meshtastic_telemetry_database.records_count; i++) {
        meshtastic_TelemetryDatabaseRecord pb = meshtastic_TelemetryDatabaseRecord_init_zero;

        pb.delivered = records[i].delivered;
        pb.telemetry = records[i].telemetry;
        meshtastic_telemetry_database.records[i] = pb;
    }

    // Encode into a memory buffer first, then write the buffer to the file under spiLock.
    LOG_DEBUG("AirQualityDatabase: Encoding records to memory buffer before writing %s", STORAGE_KEY);

    // std::vector<uint8_t> buffer(meshtastic_TelemetryDatabase_size);
    // pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_TelemetryDatabase_size};

    if (!pb_encode(&stream, meshtastic_TelemetryDatabase_fields, &meshtastic_telemetry_database)) {
        LOG_ERROR("Error: can't encode protobuf %s", PB_GET_ERROR(&stream));
    } else {
        encodedSize = stream.bytes_written;
        // size_t written = file.write(buffer.data(), encodedSize);

        if (!encodedSize) {
            // LOG_ERROR("AirQualityDatabase: Failed to write encoded data to file (wrote %u of %u)", written, encodedSize);
            LOG_ERROR("AirQualityDatabase: Failed to write encoded data to file (wrote  %u)", encodedSize);
        } else {
            okay = true;
        }
    }

    okay &= file.close();

    if (okay) {
        LOG_DEBUG("AirQualityDatabase: Saved %d records to storage (%u bytes)", records.size(), encodedSize);
    }

    return okay;
#else
    LOG_DEBUG("AirQualityDatabase: FSCom not available, skipping storage save");
    return true;
#endif
}

TelemetryDatabase<meshtastic_AirQualityMetrics>::Statistics AirQualityDatabase::getStatistics() const
{
    Statistics stats = {};

    if (records.empty()) {
        return stats;
    }

    stats.records_count = records.size();
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
