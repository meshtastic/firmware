#include "TransmitHistory.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include <Throttle.h>

#ifdef FSCom

TransmitHistory *transmitHistory = nullptr;

TransmitHistory *TransmitHistory::getInstance()
{
    if (!transmitHistory) {
        transmitHistory = new TransmitHistory();
    }
    return transmitHistory;
}

void TransmitHistory::loadFromDisk()
{
    spiLock->lock();
    auto file = FSCom.open(FILENAME, FILE_O_READ);
    if (file) {
        FileHeader header{};
        if (file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) && header.magic == MAGIC &&
            header.version == VERSION && header.count <= MAX_ENTRIES) {
            for (uint8_t i = 0; i < header.count; i++) {
                Entry entry{};
                if (file.read((uint8_t *)&entry, sizeof(entry)) == sizeof(entry)) {
                    if (entry.epochSeconds > 0) {
                        history[entry.key] = entry.epochSeconds;
                        // Seed in-memory millis so throttle works even without RTC/GPS.
                        // Treating stored entries as "just sent" is safe — worst case the
                        // node waits one full interval before its first broadcast.
                        lastMillis[entry.key] = millis();
                    }
                }
            }
            LOG_INFO("TransmitHistory: loaded %u entries from disk", header.count);
        } else {
            LOG_WARN("TransmitHistory: invalid file header, starting fresh");
        }
        file.close();
    } else {
        LOG_INFO("TransmitHistory: no history file found, starting fresh");
    }
    spiLock->unlock();
    dirty = false;
}

void TransmitHistory::setLastSentToMesh(uint16_t key)
{
    lastMillis[key] = millis();
    uint32_t now = getTime();
    if (now >= 2) {
        history[key] = now;
        dirty = true;
        // Don't flush to disk on every transmit — flash has limited write endurance.
        // The in-memory lastMillis map handles throttle during normal operation.
        // Disk is flushed: before deep sleep (sleep.cpp) and periodically here,
        // throttled to at most once per 5 minutes. Always save the first time
        // after boot so a crash-reboot loop can't avoid persisting.
        if (lastDiskSave == 0 || !Throttle::isWithinTimespanMs(lastDiskSave, SAVE_INTERVAL_MS)) {
            if (saveToDisk()) {
                lastDiskSave = millis();
            }
        }
    }
}

uint32_t TransmitHistory::getLastSentToMeshEpoch(uint16_t key) const
{
    auto it = history.find(key);
    if (it != history.end()) {
        return it->second;
    }
    return 0;
}

uint32_t TransmitHistory::getLastSentToMeshMillis(uint16_t key) const
{
    // Prefer runtime millis value (accurate within this boot)
    auto mit = lastMillis.find(key);
    if (mit != lastMillis.end()) {
        return mit->second;
    }

    // Fall back to epoch conversion (loaded from disk after reboot)
    uint32_t storedEpoch = getLastSentToMeshEpoch(key);
    if (storedEpoch == 0) {
        return 0; // No stored time — module has never sent
    }

    uint32_t now = getTime();
    if (now < 2) {
        // No valid RTC time yet — can't convert to millis. Return 0 so throttle doesn't block.
        return 0;
    }

    if (storedEpoch > now) {
        // Stored time is in the future (clock went backwards?) — treat as stale
        return 0;
    }

    uint32_t secondsAgo = now - storedEpoch;
    uint32_t msAgo = secondsAgo * 1000;

    // Guard against overflow: if the transmit was very long ago, just return 0 (won't throttle)
    if (secondsAgo > 86400 || msAgo / 1000 != secondsAgo) {
        return 0;
    }

    // Convert to a millis()-relative timestamp: millis() - msAgo
    // This gives a value that, when passed to Throttle::isWithinTimespanMs(value, interval),
    // correctly reports whether the transmit was within interval ms.
    return millis() - msAgo;
}

bool TransmitHistory::saveToDisk()
{
    if (!dirty) {
        return true;
    }

    spiLock->lock();

    FSCom.mkdir("/prefs");

    // Remove old file first
    if (FSCom.exists(FILENAME)) {
        FSCom.remove(FILENAME);
    }

    auto file = FSCom.open(FILENAME, FILE_O_WRITE);
    if (file) {
        FileHeader header{};
        header.magic = MAGIC;
        header.version = VERSION;
        header.count = (uint8_t)min((size_t)MAX_ENTRIES, history.size());

        file.write((uint8_t *)&header, sizeof(header));

        uint8_t written = 0;
        for (auto &pair : history) {
            if (written >= MAX_ENTRIES)
                break;
            Entry entry{};
            entry.key = pair.first;
            entry.epochSeconds = pair.second;
            file.write((uint8_t *)&entry, sizeof(entry));
            written++;
        }
        file.flush();
        file.close();
        LOG_DEBUG("TransmitHistory: saved %u entries to disk", written);
        dirty = false;
        spiLock->unlock();
        return true;
    } else {
        LOG_WARN("TransmitHistory: failed to open file for writing");
    }

    spiLock->unlock();
    return false;
}

#else
// No filesystem available — provide stub with in-memory tracking
TransmitHistory *transmitHistory = nullptr;

TransmitHistory *TransmitHistory::getInstance()
{
    if (!transmitHistory) {
        transmitHistory = new TransmitHistory();
    }
    return transmitHistory;
}

void TransmitHistory::loadFromDisk() {}

void TransmitHistory::setLastSentToMesh(uint16_t key)
{
    lastMillis[key] = millis();
}

uint32_t TransmitHistory::getLastSentToMeshEpoch(uint16_t key) const
{
    return 0;
}

uint32_t TransmitHistory::getLastSentToMeshMillis(uint16_t key) const
{
    auto mit = lastMillis.find(key);
    return (mit != lastMillis.end()) ? mit->second : 0;
}

bool TransmitHistory::saveToDisk()
{
    return true;
}

#endif
