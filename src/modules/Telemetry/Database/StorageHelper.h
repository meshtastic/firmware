#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "configuration.h"
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <vector>

/**
 * Helper utilities for serializing/deserializing telemetry database records
 * to/from flash storage using protobuf format
 */
namespace StorageHelper
{
/**
 * Encode TelemetryDatabaseSnapshot to a byte buffer
 * @param snapshot The snapshot to encode
 * @param buffer Output buffer for encoded data
 * @param buffer_size Maximum size of output buffer
 * @return Number of bytes written, or 0 on error
 */
inline size_t encodeSnapshot(const meshtastic_TelemetryDatabaseSnapshot &snapshot, uint8_t *buffer, size_t buffer_size)
{
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);

    if (!pb_encode(&stream, meshtastic_TelemetryDatabaseSnapshot_fields, &snapshot)) {
        LOG_ERROR("StorageHelper: Failed to encode snapshot: %s", PB_GET_ERROR(&stream));
        return 0;
    }

    return stream.bytes_written;
}

/**
 * Decode TelemetryDatabaseSnapshot from a byte buffer
 * @param buffer Buffer containing encoded data
 * @param buffer_size Size of the buffer
 * @param snapshot Output snapshot structure
 * @return true if successful, false on error
 */
inline bool decodeSnapshot(const uint8_t *buffer, size_t buffer_size, meshtastic_TelemetryDatabaseSnapshot &snapshot)
{
    pb_istream_t stream = pb_istream_from_buffer(buffer, buffer_size);

    if (!pb_decode(&stream, meshtastic_TelemetryDatabaseSnapshot_fields, &snapshot)) {
        LOG_ERROR("StorageHelper: Failed to decode snapshot: %s", PB_GET_ERROR(&stream));
        return false;
    }

    return true;
}

/**
 * Encode a single TelemetryDatabaseRecord
 * @param record The record to encode
 * @param buffer Output buffer for encoded data
 * @param buffer_size Maximum size of output buffer
 * @return Number of bytes written, or 0 on error
 */
inline size_t encodeRecord(const meshtastic_TelemetryDatabaseRecord &record, uint8_t *buffer, size_t buffer_size)
{
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);

    if (!pb_encode(&stream, meshtastic_TelemetryDatabaseRecord_fields, &record)) {
        LOG_ERROR("StorageHelper: Failed to encode record: %s", PB_GET_ERROR(&stream));
        return 0;
    }

    return stream.bytes_written;
}

/**
 * Decode a single TelemetryDatabaseRecord
 * @param buffer Buffer containing encoded data
 * @param buffer_size Size of the buffer
 * @param record Output record structure
 * @return true if successful, false on error
 */
inline bool decodeRecord(const uint8_t *buffer, size_t buffer_size, meshtastic_TelemetryDatabaseRecord &record)
{
    pb_istream_t stream = pb_istream_from_buffer(buffer, buffer_size);

    if (!pb_decode(&stream, meshtastic_TelemetryDatabaseRecord_fields, &record)) {
        LOG_ERROR("StorageHelper: Failed to decode record: %s", PB_GET_ERROR(&stream));
        return false;
    }

    return true;
}

/**
 * Helper to save snapshot to flash using provided callback
 * @param snapshot The snapshot to save
 * @param save_callback Callback function to save buffer to flash: bool(const uint8_t*, size_t)
 * @return true if successful
 */
template <typename SaveCallback>
inline bool saveSnapshotToFlash(const meshtastic_TelemetryDatabaseSnapshot &snapshot, SaveCallback save_callback)
{
    // Allocate buffer for encoded snapshot
    // Maximum size depends on max records, estimate ~200 bytes per record + overhead
    constexpr size_t MAX_SNAPSHOT_SIZE = 100 * 200 + 128;
    uint8_t buffer[MAX_SNAPSHOT_SIZE];

    size_t encoded_size = encodeSnapshot(snapshot, buffer, MAX_SNAPSHOT_SIZE);
    if (encoded_size == 0) {
        LOG_ERROR("StorageHelper: Failed to encode snapshot for storage");
        return false;
    }

    if (!save_callback(buffer, encoded_size)) {
        LOG_ERROR("StorageHelper: Failed to write snapshot to flash");
        return false;
    }

    LOG_DEBUG("StorageHelper: Saved snapshot to flash (%zu bytes)", encoded_size);
    return true;
}

/**
 * Helper to load snapshot from flash using provided callback
 * @param load_callback Callback function to load buffer from flash: bool(uint8_t*, size_t&)
 * @param snapshot Output snapshot structure
 * @return true if successful
 */
template <typename LoadCallback>
inline bool loadSnapshotFromFlash(LoadCallback load_callback, meshtastic_TelemetryDatabaseSnapshot &snapshot)
{
    constexpr size_t MAX_SNAPSHOT_SIZE = 100 * 200 + 128;
    uint8_t buffer[MAX_SNAPSHOT_SIZE];
    size_t buffer_size = 0;

    if (!load_callback(buffer, buffer_size)) {
        LOG_DEBUG("StorageHelper: No snapshot found in flash (first boot)");
        return false;
    }

    if (!decodeSnapshot(buffer, buffer_size, snapshot)) {
        LOG_ERROR("StorageHelper: Failed to decode snapshot from flash");
        return false;
    }

    LOG_DEBUG("StorageHelper: Loaded snapshot from flash (%zu bytes)", buffer_size);
    return true;
}

} // namespace StorageHelper

#endif
