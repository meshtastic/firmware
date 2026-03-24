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

    /**
     * Get the last transmit epoch seconds for a given key, or 0 if unknown.
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

  private:
    TransmitHistory() = default;

    static constexpr const char *FILENAME = "/prefs/transmit_history.dat";
    static constexpr uint32_t MAGIC = 0x54485354; // "THST"
    static constexpr uint8_t VERSION = 1;
    static constexpr uint8_t MAX_ENTRIES = 16;
    static constexpr uint32_t SAVE_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

    struct __attribute__((packed)) Entry {
        uint16_t key;
        uint32_t epochSeconds;
    };

    struct __attribute__((packed)) FileHeader {
        uint32_t magic;
        uint8_t version;
        uint8_t count;
    };

    std::map<uint16_t, uint32_t> history;    // key -> epoch seconds (for disk persistence)
    std::map<uint16_t, uint32_t> lastMillis; // key -> millis() value (for runtime throttle)
    bool dirty = false;
    uint32_t lastDiskSave = 0; // millis() of last disk flush
};

extern TransmitHistory *transmitHistory;
