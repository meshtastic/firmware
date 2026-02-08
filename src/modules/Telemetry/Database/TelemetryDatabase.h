#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "configuration.h"
#include <cstdint>
#include <cstring>
#include <vector>

/**
* Database record structure
* Uses protobuf TelemetryDatabaseRecord message for serialization
*/
struct DatabaseRecord {
    meshtastic_Telemetry telemetry; // Telemetry data
    bool delivered;                 // Whether this record has been delivered
};

/**
 * Base class for telemetry database storage
 * Provides a template for storing historical telemetry data with metadata
 * Uses protobuf messages for serialization and storage
 */
template <typename TelemetryType> class TelemetryDatabase
{
  public:

    /**
     * Statistics about stored data
     */
    struct Statistics {
        uint32_t records_count;  // Total number of records
        uint32_t min_timestamp; // Oldest record timestamp
        uint32_t max_timestamp; // Newest record timestamp
        uint32_t delivered;     // Count delivered records
    };

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
    virtual bool markDelivered(uint32_t index) = 0;

    /**
     * Mark all records as delivered to mesh
     * @return true if successful
     */
    virtual bool markAllDelivered() = 0;

    /**
     * Get the number of records in the database
     * @return The record count
     */
    virtual uint32_t getRecordCount() const = 0;

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

    /**
     * Get records not yet delivered via MQTT (for MQTT recovery when connected)
     * @return Vector of records that need MQTT delivery
     */
    virtual std::vector<DatabaseRecord> getRecordsForRecovery() const = 0;
};
