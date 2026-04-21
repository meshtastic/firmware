#pragma once

#include "configuration.h"
#include <Arduino.h>
#include <map>

/**
 * TransmitHistory persists the last broadcast transmit time (epoch seconds) per portnum
 * to the filesystem so that throttle checks survive reboots/crashes.
 *
 * On boot, modules call getLastSentToMeshMillis() to recover a millis()-relative timestamp
 * from the stored epoch time, which plugs directly into existing throttle logic.
 *
 * On every broadcast transmit, modules call setLastSentToMesh() which updates the
 * in-memory cache and flushes to disk.
 *
 * Keys are meshtastic_PortNum values (one entry per portnum).
 */

#include "mesh/generated/meshtastic/portnums.pb.h"

class TransmitHistory
{
  public:
    static TransmitHistory *getInstance();

    /**
     * Load persisted transmit times from disk. Call once during init after filesystem is ready.
     */
    void loadFromDisk();

    /**
     * Record that a broadcast was sent for the given key right now.
     * Stores epoch seconds and flushes to disk.
     */
    void setLastSentToMesh(uint16_t key);

#ifdef PIO_UNIT_TESTING
    /**
     * Directly set the stored epoch for a key without touching the runtime lastMillis map.
     * Intended for testing purposes: lets tests simulate "the last broadcast happened N
     * seconds ago" without needing to fake the system clock.
     */
    void setLastSentAtEpoch(uint16_t key, uint32_t epochSeconds);

    /**
     * Directly set a boot-relative timestamp (seconds since boot) for testing.
     */
    void setLastSentAtBootRelative(uint16_t key, uint32_t secondsSinceBoot);
#endif

    /**
     * Get the raw persisted timestamp seconds for a given key, or 0 if unknown.
     *
     * The returned value is an absolute epoch when persisted with valid RTC/NTP/GPS time,
     * or boot-relative seconds when ENTRY_FLAG_BOOT_RELATIVE is set.
     */
    uint32_t getLastSentToMeshEpoch(uint16_t key) const;

    /**
     * Convert a stored epoch timestamp into a millis()-relative timestamp suitable
     * for use with Throttle::isWithinTimespanMs().
     *
     * Returns 0 if no valid time is stored or if the stored time is in the future
     * (which shouldn't happen but guards against clock weirdness).
     *
     * Example: if the stored epoch is 300 seconds ago, and millis() is currently 10000,
     * this returns 10000 - 300000 (wrapped appropriately for uint32_t arithmetic).
     */
    uint32_t getLastSentToMeshMillis(uint16_t key) const;

    /**
     * Flush dirty entries to disk. Called periodically or on demand.
     *
     * @return true if the data is persisted (or there was nothing to write), false on write/open failure.
     */
    bool saveToDisk();

    /**
     * Wipe in-memory throttle state + remove the on-disk file. Required
     * alongside rmDir("/prefs") in factoryReset — otherwise the 5-min
     * auto-flush resurrects the file from the still-populated maps.
     */
    void clear();

  private:
    TransmitHistory() = default;

    static constexpr const char *FILENAME = "/prefs/transmit_history.dat";
    static constexpr uint32_t MAGIC = 0x54485354; // "THST"
    static constexpr uint8_t VERSION = 2;
    static constexpr uint8_t MAX_ENTRIES = 16;
    static constexpr uint32_t SAVE_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes
    static constexpr uint32_t BOOT_RELATIVE_RECOVERY_WINDOW_SEC = 2 * 60;
    static constexpr uint32_t LEGACY_BOOT_RELATIVE_MAX_SEC = 365UL * 24 * 60 * 60;

    enum EntryFlags : uint8_t {
        ENTRY_FLAG_NONE = 0,
        ENTRY_FLAG_BOOT_RELATIVE = 0x01,
    };

    struct StoredTimestamp {
        uint32_t seconds = 0;
        uint8_t flags = ENTRY_FLAG_NONE;
    };

    struct __attribute__((packed)) Entry {
        uint16_t key;
        uint32_t epochSeconds;
        uint8_t flags;
    };

    struct __attribute__((packed)) LegacyEntry {
        uint16_t key;
        uint32_t epochSeconds;
    };

    struct __attribute__((packed)) FileHeader {
        uint32_t magic;
        uint8_t version;
        uint8_t count;
    };

    uint32_t getLastSentAbsoluteMillis(uint32_t storedEpoch) const;
    uint32_t getLastSentBootRelativeMillis(uint32_t storedSeconds) const;
    static StoredTimestamp makeStoredTimestamp(uint32_t seconds, uint8_t flags = ENTRY_FLAG_NONE);
    static StoredTimestamp decodeLegacyTimestamp(uint32_t seconds);

    std::map<uint16_t, StoredTimestamp> history; // key -> persisted transmit time
    std::map<uint16_t, uint32_t> lastMillis;     // key -> millis() value (for runtime throttle)
    bool dirty = false;
    uint32_t lastDiskSave = 0; // millis() of last disk flush
};

extern TransmitHistory *transmitHistory;
