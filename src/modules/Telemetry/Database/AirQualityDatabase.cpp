#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "AirQualityDatabase.h"
#include "RTC.h"
#include <cmath>
#include <cstring>

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

    // Save to storage after each addition
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

    records[index].flags.delivered_to_mesh = 1;
    return saveToStorage();
}

bool AirQualityDatabase::markDeliveredToMqtt(uint32_t index)
{
    concurrency::Lock lock(recordsLock);

    if (index >= records.size()) {
        return false;
    }

    records[index].flags.delivered_to_mqtt = 1;
    return saveToStorage();
}

bool AirQualityDatabase::markAllDeliveredToMesh()
{
    concurrency::Lock lock(recordsLock);

    for (auto &record : records) {
        record.flags.delivered_to_mesh = 1;
    }

    return saveToStorage();
}

bool AirQualityDatabase::markAllDeliveredToMqtt()
{
    concurrency::Lock lock(recordsLock);

    for (auto &record : records) {
        record.flags.delivered_to_mqtt = 1;
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
    // For now, we'll use RAM-only storage
    // Full flash implementation would use LittleFS or similar
    LOG_DEBUG("AirQualityDatabase: Loaded from storage (RAM only)");
    return true;
}

bool AirQualityDatabase::saveToStorage()
{
    // For now, we'll use RAM-only storage
    // Full flash implementation would use LittleFS or similar
    // LOG_DEBUG("AirQualityDatabase: Saved to storage (RAM only)");
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
