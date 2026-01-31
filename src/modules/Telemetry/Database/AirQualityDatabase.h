#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "StorageHelper.h"
#include "TelemetryDatabase.h"
#include <deque>
#include <vector>

/**
 * Air Quality Telemetry Database
 * Stores historical air quality measurements with delivery status tracking
 * Uses protobuf serialization for persistent storage in flash
 */
class AirQualityDatabase : public TelemetryDatabase<meshtastic_AirQualityMetrics>
{
  private:
    static constexpr uint32_t MAX_RECORDS = 100; // Maximum records to store in memory
    static constexpr const char *STORAGE_KEY = "air_quality_db";

    std::deque<DatabaseRecord> records;
    mutable concurrency::Lock recordsLock;

    /**
     * Convert DatabaseRecord to protobuf format
     */
    meshtastic_TelemetryDatabaseRecord recordToProtobuf(const DatabaseRecord &record) const;

    /**
     * Convert protobuf record to DatabaseRecord
     */
    DatabaseRecord recordFromProtobuf(const meshtastic_TelemetryDatabaseRecord &pb) const;

  public:
    AirQualityDatabase();
    virtual ~AirQualityDatabase() = default;

    /**
     * Initialize the database
     */
    bool init() override;

    /**
     * Add a new record to the database
     */
    bool addRecord(const DatabaseRecord &record) override;

    /**
     * Get a record by index
     */
    bool getRecord(uint32_t index, DatabaseRecord &record) const override;

    /**
     * Get all records as a vector
     */
    std::vector<DatabaseRecord> getAllRecords() const override;

    /**
     * Mark a record as delivered to mesh
     */
    bool markDeliveredToMesh(uint32_t index) override;

    /**
     * Mark a record as delivered via MQTT
     */
    bool markDeliveredToMqtt(uint32_t index) override;

    /**
     * Mark all records as delivered to mesh
     */
    bool markAllDeliveredToMesh() override;

    /**
     * Mark all records as delivered via MQTT
     */
    bool markAllDeliveredToMqtt() override;

    /**
     * Get the number of records in the database
     */
    uint32_t getRecordCount() const override;

    /**
     * Get the maximum number of records this database can hold
     */
    uint32_t getMaxRecords() const override { return MAX_RECORDS; }

    /**
     * Clear all records from the database
     */
    bool clearAll() override;

    /**
     * Load from persistent storage (flash)
     * Uses protobuf deserialization
     */
    bool loadFromStorage() override;

    /**
     * Save to persistent storage (flash)
     * Uses protobuf serialization via TelemetryDatabaseSnapshot
