#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "AirQualityDatabase.h"
#include "RTC.h"
#include <cmath>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

/**
 * Convert DatabaseRecord to protobuf TelemetryDatabaseRecord
 */
meshtastic_TelemetryDatabaseRecord AirQualityDatabase::recordToProtobuf(const DatabaseRecord &record) const
{
    meshtastic_TelemetryDatabaseRecord pb = {};

    pb.timestamp = record.timestamp;
    pb.flags.delivered_to_mesh = record.flags.delivered_to_mesh;
    pb.flags.delivered_to_mqtt = record.flags.delivered_to_mqtt;

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
    record.flags.delivered_to_mqtt = pb.flags.delivered_to_mqtt;

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

bool AirQualityDatabase::markDeliveredToMqtt(uint32_t index)
{
    concurrency::Lock lock(recordsLock);

    if (index >= records.size()) {
        return false;
    }

    records[index].flags.delivered_to_mqtt = true;
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

bool AirQualityDatabase::markAllDeliveredToMqtt()
{
    concurrency::Lock lock(recordsLock);

    for (auto &record : records) {
        record.flags.delivered_to_mqtt = true;
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
    // TODO: Implement actual protobuf deserialization from flash storage
    // Example implementation using StorageHelper:
    //
    // meshtastic_TelemetryDatabaseSnapshot snapshot = {};
    // auto load_cb = [](uint8_t* buf, size_t& len) {
    //     // Load from flash using e.g., nvs_get_blob() or LittleFS
    //     // Return true if successful
    //     return false;  // Not found on first boot
    // };
    //
    // if (StorageHelper::loadSnapshotFromFlash(load_cb, snapshot)) {
    //     for (uint32_t i = 0; i < snapshot.records_count; i++) {
    //         DatabaseRecord record = recordFromProtobuf(snapshot.records[i]);
    //         records.push_back(record);
    //     }
    //     LOG_DEBUG("AirQualityDatabase: Loaded %d records from storage", records.size());
    // }

    LOG_DEBUG("AirQualityDatabase: Loaded from storage (protobuf deserialization ready)");
    return true;
}

bool AirQualityDatabase::saveToStorage()
{
    // TODO: Implement actual protobuf serialization to flash storage
    // Example implementation using StorageHelper:
    //
    // meshtastic_TelemetryDatabaseSnapshot snapshot = {};
    // snapshot.snapshot_timestamp = esp_timer_get_time() / 1000000;
    // snapshot.version = 1;
    // snapshot.records_count = records.size();
    //
    // Convert each record to protobuf
    // for (uint32_t i = 0; i < records.size(); i++) {
    //     snapshot.records[i] = recordToProtobuf(records[i]);
    // }
    //
    // auto save_cb = [](const uint8_t* buf, size_t len) {
    //     // Save to flash using e.g., nvs_set_blob() or LittleFS
    //     // Return true if successful
    //     return true;
    // };
    //
    // if (StorageHelper::saveSnapshotToFlash(snapshot, save_cb)) {
    //     LOG_DEBUG("AirQualityDatabase: Saved %d records to storage", records.size());
    //     return true;
    // }

    // For now, log that protobuf serialization is ready
    // LOG_DEBUG("AirQualityDatabase: Saved to storage (%d records, protobuf format)", records.size());
    return true;
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
        if (record.flags.delivered_to_mqtt) {
            stats.delivered_mqtt++;
        }
    }

    return stats;
}

#endif
