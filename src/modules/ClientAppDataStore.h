#pragma once

#include "meshtastic/admin.pb.h"
#include "meshtastic/localonly.pb.h"
#include <stddef.h>

/**
 * Bounded local storage for opaque, app-owned metadata records that
 * companion applications persist on the locally-connected node via
 * AdminMessage. See meshtastic/admin.proto's ClientAppData message for
 * the full namespaced-not-owned caveat: the firmware enforces shape,
 * payload size, and record-count limits but does NOT authenticate which
 * companion application is writing. Any admin-capable client may
 * overwrite or delete any app_id. Records are local-node-only, never
 * broadcast over LoRa, never included in NodeInfo, never relayed via
 * MQTT, and never interpreted by the firmware.
 *
 * Persisted to /prefs/clientappdata.proto (NodeDB::clientAppDataFileName)
 * and cleared automatically by factoryReset() via the existing
 * rmDir("/prefs") path.
 */
class ClientAppDataStore
{
  public:
    enum class Result {
        Ok,
        NotFound,
        InvalidAppId,
        PayloadTooLarge,
        NoSpace,
        StorageError
    };

    /**
     * Construct the global store and load any existing records from
     * /prefs/clientappdata.proto. Safe to call before NodeDB has been
     * fully initialized; a missing or corrupt file yields an empty store.
     */
    static void init();

    /**
     * Reset the in-memory table to empty. Does NOT delete the on-disk
     * file; factoryReset() handles that via rmDir("/prefs").
     */
    static void clear();

    /**
     * Copy the record matching appId into *out.
     * @return Ok on hit, NotFound if no record matches, InvalidAppId
     *         if appId is null/empty/malformed.
     */
    Result get(const char *appId, meshtastic_ClientAppData *out) const;

    /**
     * Insert or overwrite a record. Overwriting an existing app_id reuses
     * the same slot and does not consume capacity. updated_at is set to
     * the current valid wall-clock time, or 0 if the firmware has no
     * valid time yet. The caller-supplied updated_at is ignored.
     * @return Ok on success and persistence; InvalidAppId / PayloadTooLarge
     *         / NoSpace / StorageError on failure.
     */
    Result set(const meshtastic_ClientAppData &record);

    /**
     * Remove the record matching appId. Compacts the slot table so a
     * later set() can reuse the freed capacity. Deleting a missing
     * appId returns NotFound (callers may treat as idempotent success).
     */
    Result remove(const char *appId);

    /**
     * Validate an app_id against ^[a-z0-9._-]{1,32}$. Public for tests.
     * Returns false for null, empty, or any character outside the set.
     */
    static bool isValidAppId(const char *appId);

    /**
     * Number of records currently stored (0..kMaxRecords).
     */
    size_t recordCount() const;

    static constexpr size_t kMaxRecords = 4;
    static constexpr size_t kMaxPayloadBytes = 512;
    static constexpr size_t kMaxAppIdLen = 32;

  private:
    meshtastic_LocalClientAppData store_ = meshtastic_LocalClientAppData_init_zero;

    bool persistToDisk_();
    int findIndex_(const char *appId) const;
};

extern ClientAppDataStore *clientAppDataStore;
