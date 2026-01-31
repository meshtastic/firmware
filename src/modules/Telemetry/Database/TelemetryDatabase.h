#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "configuration.h"
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * Base class for telemetry database storage
 * Provides a template for storing historical telemetry data with metadata
 * Uses protobuf messages for serialization and storage
 */
template <typename TelemetryType> class TelemetryDatabase
{
  public:
    /**
     * Record flags to track delivery status
     * Uses protobuf TelemetryRecordFlags message
     */
    using RecordFlags = meshtastic_TelemetryRecordFlags;

    /**
     * Database record structure
     * Uses protobuf TelemetryDatabaseRecord message for serialization
     */
    struct DatabaseRecord {
        uint32_t timestamp;      // When the reading was taken
        TelemetryType telemetry; // The actual telemetry data
        RecordFlags flags;       // Delivery status flags
    };

    /**
     * Statistics about stored data
     */
    struct Statistics {
        uint32_t record_count;   // Total number of records
        uint32_t min_timestamp;  // Oldest record timestamp
        uint32_t max_timestamp;  // Newest record timestamp
        uint32_t delivered_mesh; // Count delivered to mesh
        uint32_t delivered_mqtt; // Count delivered via MQTT
    };

    /**
     * Helper to serialize database to protobuf snapshot
     * @param records The records to serialize
     * @return Serialized protobuf snapshot
     */
    static meshtastic_TelemetryDatabaseSnapshot serializeToProtobuf(const std::vector<DatabaseRecord> &records)
    {
        meshtastic_TelemetryDatabaseSnapshot snapshot = {};
        snapshot.snapshot_timestamp = esp_timer_get_time() / 1000000; // Current time in seconds
        snapshot.version = 1;

        // Note: Actual implementation in derived classes
        return snapshot;
    }

    /**
     * Helper to deserialize protobuf snapshot to database records
     * @param snapshot The protobuf snapshot
     * @return Vector of deserialized records
     */
    static std::vector<DatabaseRecord> deserializeFromProtobuf(const meshtastic_TelemetryDatabaseSnapshot &snapshot)
    {
        std::vector<DatabaseRecord> records;
        // Note: Actual implementation in derived classes
        return records;
    }

    virtual ~TelemetryDatabase() = default;

    /**
     * Initialize the database
     * @return true if successful
     */
    virtual bool init() = 0;

    /**
     * Add a new record to the database
     * @param record The record to add
     * @return true if successful
     */
    virtual bool addRecord(const DatabaseRecord &record) = 0;

    /**
     * Get a record by index
     * @param index The index of the record (0 = oldest)
     * @param record Output parameter for the record
     * @return true if the record exists
     */
    virtual bool getRecord(uint32_t index, DatabaseRecord &record) const = 0;

    /**
     * Get all records as a vector
     * @return Vector of all records
     */
    virtual std::vector<DatabaseRecord> getAllRecords() const = 0;

    /**
     * Mark a record as delivered to mesh
     * @param index The index of the record
     * @return true if successful
     */
    virtual bool markDeliveredToMesh(uint32_t index) = 0;

    /**
     * Mark a record as delivered via MQTT
     * @param index The index of the record
     * @return true if successful
     */
    virtual bool markDeliveredToMqtt(uint32_t index) = 0;

    /**
     * Mark all records as delivered to mesh
     * @return true if successful
     */
    virtual bool markAllDeliveredToMesh() = 0;

    /**
     * Mark all records as delivered via MQTT
     * @return true if successful
     */
    virtual bool markAllDeliveredToMqtt() = 0;

    /**
     * Get the number of records in the database
     * @return The record count
     */
    virtual uint32_t getRecordCount() const = 0;

    /**
     * Get the maximum number of records this database can hold
     * @return The maximum capacity
     */
    virtual uint32_t getMaxRecords() const = 0;

    /**
     * Clear all records from the database
     * @return true if successful
     */
    virtual bool clearAll() = 0;

    /**
     * Load from persistent storage (flash)
     * @return true if successful
     */
    virtual bool loadFromStorage() = 0;

    /**
     * Save to persistent storage (flash)
     * @return true if successful
     */
    virtual bool saveToStorage() = 0;

    /**
     * Get statistics about the stored data
     * @return Statistics struct with aggregated data
     */
    virtual Statistics getStatistics() const = 0;
};
