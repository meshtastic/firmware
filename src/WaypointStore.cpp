#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "WaypointStore.h"
#include "concurrency/LockGuard.h"
#include "gps/RTC.h"
#include "meshUtils.h"
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace
{

constexpr uint8_t WAYPOINT_STORE_VERSION = 3;
constexpr const char *WAYPOINT_STORE_FILENAME = "/Waypoints_default.wpts";

#ifndef WAYPOINT_AUTOSAVE_INTERVAL_SEC
#define WAYPOINT_AUTOSAVE_INTERVAL_SEC (2 * 60 * 60)
#endif

struct __attribute__((packed)) StoredWaypointRecord {
    uint32_t creatorNodeNum;
    uint32_t receivedTime;
    uint8_t notificationPreferences;
    uint16_t payloadLength;
    uint8_t payload[meshtastic_Waypoint_size];
};

bool decodeWaypointPayload(const uint8_t *payload, size_t payloadLength, meshtastic_Waypoint &wp)
{
    memset(&wp, 0, sizeof(wp));
    return pb_decode_from_bytes(payload, payloadLength, &meshtastic_Waypoint_msg, &wp);
}

uint16_t encodeWaypointPayload(const meshtastic_Waypoint &wp, uint8_t *payload, size_t payloadCapacity)
{
    return (uint16_t)pb_encode_to_bytes(payload, payloadCapacity, &meshtastic_Waypoint_msg, &wp);
}

static bool g_waypointStoreHasUnsavedChanges = false;
static uint32_t g_lastWaypointAutoSaveMs = 0;

uint32_t autosaveIntervalMs()
{
    uint32_t sec = (uint32_t)WAYPOINT_AUTOSAVE_INTERVAL_SEC;
    if (sec < 60)
        sec = 60;
    return sec * 1000UL;
}

void markWaypointStoreUnsaved()
{
    g_waypointStoreHasUnsavedChanges = true;
    if (g_lastWaypointAutoSaveMs == 0)
        g_lastWaypointAutoSaveMs = Time::getMillis();
}

void persistWaypointStore()
{
    LOG_INFO("Autosaving WaypointStore to flash");
    waypointStore.saveToFlash();
}

} // namespace

WaypointStore waypointStore;

void WaypointStore::notifyChanged()
{
    notifyObservers(this);
}

bool WaypointStore::isExpired(const meshtastic_Waypoint &wp, uint32_t now)
{
    // getTime() counts from boot until the RTC is set, which reads every real expiry as future.
    if (now == 0)
        now = getValidTime(RTCQuality::RTCQualityDevice);

    return !waypointIsActive(wp.expire, now);
}

bool WaypointStore::isExpired(const StoredWaypoint &entry, uint32_t now)
{
    return isExpired(entry.waypoint, now);
}

uint8_t WaypointStore::notificationPreferencesFromWaypoint(const meshtastic_Waypoint &wp)
{
    uint8_t preferences = 0;
    if (wp.notify_on_enter)
        preferences |= WAYPOINT_NOTIFY_ENTER;
    if (wp.notify_on_exit)
        preferences |= WAYPOINT_NOTIFY_EXIT;
    if (wp.notify_favorites_only)
        preferences |= WAYPOINT_NOTIFY_FAVORITES_ONLY;
    return preferences;
}

uint8_t WaypointStore::mergeNotificationPreferences(bool locallyAuthored, bool hasExisting, uint8_t existingPreferences,
                                                    const meshtastic_Waypoint &incoming)
{
    if (locallyAuthored)
        return notificationPreferencesFromWaypoint(incoming);
    return hasExisting ? existingPreferences : 0;
}

void WaypointStore::clearWireNotificationPreferences(meshtastic_Waypoint &wp)
{
    wp.notify_on_enter = false;
    wp.notify_on_exit = false;
    wp.notify_favorites_only = false;
}

const StoredWaypoint *WaypointStore::findWaypoint(uint32_t id) const
{
    for (const StoredWaypoint &entry : waypoints) {
        if (entry.waypoint.id == id)
            return &entry;
    }
    return nullptr;
}

bool WaypointStore::removeWaypointById(uint32_t id)
{
    for (auto it = waypoints.begin(); it != waypoints.end(); ++it) {
        if (it->waypoint.id == id) {
            waypoints.erase(it);
            return true;
        }
    }

    return false;
}

bool WaypointStore::removeWaypoint(uint32_t id)
{
    const bool removed = removeWaypointById(id);
    if (!removed)
        return false;

#if ENABLE_WAYPOINT_PERSISTENCE
    markWaypointStoreUnsaved();
#endif
    notifyChanged();

    return true;
}

bool WaypointStore::setNotificationPreference(uint32_t id, WaypointNotificationPreference preference, bool enabled)
{
    for (StoredWaypoint &entry : waypoints) {
        if (entry.waypoint.id != id)
            continue;

        const uint8_t previous = entry.notificationPreferences;
        if (enabled)
            entry.notificationPreferences |= preference;
        else
            entry.notificationPreferences &= ~preference;
        if (entry.notificationPreferences == previous)
            return true;

#if ENABLE_WAYPOINT_PERSISTENCE
        markWaypointStoreUnsaved();
#endif
        notifyChanged();
        return true;
    }

    return false;
}

void WaypointStore::addStoredWaypoint(const StoredWaypoint &entry)
{
    removeWaypointById(entry.waypoint.id);

    waypoints.push_front(entry);
    while (waypoints.size() > WAYPOINT_HISTORY_LIMIT)
        waypoints.pop_back();
}

bool WaypointStore::addFromPacket(const meshtastic_MeshPacket &packet, bool locallyAuthored, StoredWaypoint *stored)
{
    StoredWaypoint entry;
    if (!decodeWaypointPayload(packet.decoded.payload.bytes, packet.decoded.payload.size, entry.waypoint))
        return false;

    const StoredWaypoint *existing = findWaypoint(entry.waypoint.id);
    entry.notificationPreferences = mergeNotificationPreferences(
        locallyAuthored, existing != nullptr, existing ? existing->notificationPreferences : 0, entry.waypoint);
    clearWireNotificationPreferences(entry.waypoint);
    entry.receivedTime = packet.rx_time ? packet.rx_time : getTime();
    entry.creatorNodeNum = getFrom(&packet);

    if (stored)
        *stored = entry;

    // rx_time holds uptime, not an epoch, when has_rx_time is false; pass 0 so isExpired() resolves
    // the clock itself rather than comparing an expiry against seconds since boot.
    if (isExpired(entry, packet.has_rx_time ? packet.rx_time : 0)) {
        // Respect the lock: only the node a waypoint is locked to may delete it on our device.
        // An unauthorized deletion attempt is ignored entirely, rather than applied locally.
        for (const auto &storedEntry : waypoints) {
            if (storedEntry.waypoint.id != entry.waypoint.id)
                continue;
            if (storedEntry.waypoint.locked_to != 0 && storedEntry.waypoint.locked_to != entry.creatorNodeNum)
                return true; // Packet handled, but the deletion is not honored
            break;
        }

        removeWaypoint(entry.waypoint.id);
        return true;
    }

    addStoredWaypoint(entry);

#if ENABLE_WAYPOINT_PERSISTENCE
    markWaypointStoreUnsaved();
#endif
    notifyChanged();

    return true;
}

bool WaypointStore::purgeExpired(uint32_t now)
{
    // No local clock normalization: isExpired() owns that policy, including the delete convention.
    bool changed = false;
    for (auto it = waypoints.begin(); it != waypoints.end();) {
        if (!isExpired(*it, now)) {
            ++it;
            continue;
        }

        it = waypoints.erase(it);
        changed = true;
    }

    if (changed) {
#if ENABLE_WAYPOINT_PERSISTENCE
        markWaypointStoreUnsaved();
#endif
        notifyChanged();
    }

    return changed;
}

void WaypointStore::saveToFlash()
{
    purgeExpired();

#if ENABLE_WAYPOINT_PERSISTENCE && defined(FSCom)
    if (!g_waypointStoreHasUnsavedChanges)
        return;

    spiLock->lock();
    FSCom.mkdir("/");
    spiLock->unlock();

    SafeFile f(WAYPOINT_STORE_FILENAME, false);

    spiLock->lock();
    const uint8_t version = WAYPOINT_STORE_VERSION;
    size_t countFull = waypoints.size();
    if (countFull > WAYPOINT_HISTORY_LIMIT)
        countFull = WAYPOINT_HISTORY_LIMIT;
    if (countFull > UINT8_MAX)
        countFull = UINT8_MAX;
    const uint8_t count = (uint8_t)countFull;

    f.write(&version, 1);
    f.write(&count, 1);

    for (uint8_t i = 0; i < count; ++i) {
        StoredWaypointRecord rec = {};
        rec.creatorNodeNum = waypoints[i].creatorNodeNum;
        rec.receivedTime = waypoints[i].receivedTime;
        rec.notificationPreferences = waypoints[i].notificationPreferences;
        rec.payloadLength = encodeWaypointPayload(waypoints[i].waypoint, rec.payload, sizeof(rec.payload));
        f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
    }
    spiLock->unlock();
    f.close();
#endif

#if ENABLE_WAYPOINT_PERSISTENCE
    g_waypointStoreHasUnsavedChanges = false;
    g_lastWaypointAutoSaveMs = Time::getMillis();
#endif
}

void WaypointStore::loadFromFlash()
{
    std::deque<StoredWaypoint>().swap(waypoints);

#if ENABLE_WAYPOINT_PERSISTENCE && defined(FSCom)
    {
        concurrency::LockGuard guard(spiLock);

        if (FSCom.exists(WAYPOINT_STORE_FILENAME)) {
            auto f = FSCom.open(WAYPOINT_STORE_FILENAME, FILE_O_READ);
            if (f) {
                uint8_t version = 0;
                uint8_t count = 0;
                f.readBytes(reinterpret_cast<char *>(&version), 1);
                f.readBytes(reinterpret_cast<char *>(&count), 1);

                if (version != WAYPOINT_STORE_VERSION) {
                    LOG_WARN("WaypointStore version mismatch (%u)", version);
                    f.close();
                } else {
                    if (count > WAYPOINT_HISTORY_LIMIT)
                        count = WAYPOINT_HISTORY_LIMIT;

                    for (uint8_t i = 0; i < count; ++i) {
                        StoredWaypoint entry;
                        StoredWaypointRecord rec = {};
                        if (f.readBytes(reinterpret_cast<char *>(&rec), sizeof(rec)) != sizeof(rec))
                            break;
                        if (rec.payloadLength == 0 || rec.payloadLength > sizeof(rec.payload)) {
                            LOG_WARN("WaypointStore skipping corrupt record %u", i);
                            continue;
                        }
                        if (!decodeWaypointPayload(rec.payload, rec.payloadLength, entry.waypoint))
                            continue;
                        entry.receivedTime = rec.receivedTime;
                        entry.creatorNodeNum = rec.creatorNodeNum;
                        entry.notificationPreferences = rec.notificationPreferences;

                        if (isExpired(entry.waypoint))
                            continue;
                        waypoints.push_back(entry);
                    }
                    f.close();
                }
            }
        }
    }
#endif

#if ENABLE_WAYPOINT_PERSISTENCE
    g_waypointStoreHasUnsavedChanges = false;
    g_lastWaypointAutoSaveMs = Time::getMillis();
#endif
}

void WaypointStore::clearAllWaypoints()
{
    const bool hadWaypoints = !waypoints.empty();

    std::deque<StoredWaypoint>().swap(waypoints);

#if ENABLE_WAYPOINT_PERSISTENCE && defined(FSCom)
    SafeFile f(WAYPOINT_STORE_FILENAME, false);
    {
        concurrency::LockGuard guard(spiLock);
        const uint8_t version = WAYPOINT_STORE_VERSION;
        const uint8_t count = 0;
        f.write(&version, 1);
        f.write(&count, 1);
    }
    f.close();
#endif

#if ENABLE_WAYPOINT_PERSISTENCE
    g_waypointStoreHasUnsavedChanges = false;
    g_lastWaypointAutoSaveMs = Time::getMillis();
#endif

    if (hadWaypoints)
        notifyChanged();
}

#if ENABLE_WAYPOINT_PERSISTENCE
void waypointStoreAutosaveTick()
{
    if (!g_waypointStoreHasUnsavedChanges) {
        if (g_lastWaypointAutoSaveMs == 0)
            g_lastWaypointAutoSaveMs = Time::getMillis();
        return;
    }

    if (g_lastWaypointAutoSaveMs == 0) {
        g_lastWaypointAutoSaveMs = Time::getMillis();
        return;
    }

    Throttle::execute(&g_lastWaypointAutoSaveMs, autosaveIntervalMs(), persistWaypointStore);
}
#endif

#endif
