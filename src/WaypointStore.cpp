#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "Throttle.h"
#include "WaypointStore.h"
#include "concurrency/LockGuard.h"
#include "gps/RTC.h"
#include "modules/GeofenceModule.h"
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

#include "mesh/NodeDB.h"

namespace
{

constexpr uint8_t WAYPOINT_STORE_VERSION = 2;

#ifndef WAYPOINT_AUTOSAVE_INTERVAL_SEC
#define WAYPOINT_AUTOSAVE_INTERVAL_SEC (2 * 60 * 60)
#endif

struct __attribute__((packed)) StoredWaypointRecord {
    uint32_t creatorNodeNum;
    uint32_t receivedTime;
    uint16_t payloadLength;
    uint8_t payload[meshtastic_Waypoint_size];
};

struct __attribute__((packed)) StoredWaypointRecordV1 {
    uint32_t receivedTime;
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
        g_lastWaypointAutoSaveMs = millis();
}

void persistWaypointStore()
{
    LOG_INFO("Autosaving WaypointStore to flash");
    waypointStore.saveToFlash();
}

} // namespace

WaypointStore waypointStore("default");

WaypointStore::WaypointStore(const std::string &label)
{
    filename = "/Waypoints_" + label + ".wpts";
}

void WaypointStore::notifyChanged()
{
    notifyObservers(this);
}

uint32_t WaypointStore::age(const StoredWaypoint &entry)
{
    const uint32_t now = getTime();
    if (entry.receivedTime == 0 || now <= entry.receivedTime)
        return 0;

    return now - entry.receivedTime;
}

bool WaypointStore::isExpired(const meshtastic_Waypoint &wp, uint32_t now)
{
    if (wp.expire == 0)
        return false;

    if (now == 0)
        now = getTime();

    return now != 0 && wp.expire <= now;
}

bool WaypointStore::isExpired(const StoredWaypoint &entry, uint32_t now)
{
    return isExpired(entry.waypoint, now);
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

void WaypointStore::addStoredWaypoint(const StoredWaypoint &entry)
{
    removeWaypointById(entry.waypoint.id);

    waypoints.push_front(entry);
    while (waypoints.size() > WAYPOINT_HISTORY_LIMIT)
        waypoints.pop_back();
}

bool WaypointStore::addFromPacket(const meshtastic_MeshPacket &packet, StoredWaypoint *stored)
{
    StoredWaypoint entry;
    if (!decodeWaypointPayload(packet.decoded.payload.bytes, packet.decoded.payload.size, entry.waypoint))
        return false;

    entry.receivedTime = packet.rx_time ? packet.rx_time : getTime();
    entry.creatorNodeNum = packet.from;

    if (stored)
        *stored = entry;

    if (isExpired(entry)) {
        const bool removed = removeWaypointById(entry.waypoint.id);
#if ENABLE_WAYPOINT_PERSISTENCE
        if (removed)
            markWaypointStoreUnsaved();
#endif
        if (removed)
            notifyChanged();
        return true;
    }

    addStoredWaypoint(entry);

#if ENABLE_WAYPOINT_PERSISTENCE
    markWaypointStoreUnsaved();
#endif
    notifyChanged();

    return true;
}

void WaypointStore::addWaypoint(const meshtastic_Waypoint &wp, uint32_t receivedTime, NodeNum creatorNodeNum)
{
    StoredWaypoint entry;
    entry.waypoint = wp;
    entry.receivedTime = receivedTime ? receivedTime : getTime();
    entry.creatorNodeNum = creatorNodeNum ? creatorNodeNum : (nodeDB ? nodeDB->getNodeNum() : 0);

    if (isExpired(entry)) {
        const bool removed = removeWaypointById(entry.waypoint.id);
#if ENABLE_WAYPOINT_PERSISTENCE
        if (removed)
            markWaypointStoreUnsaved();
#endif
        if (removed)
            notifyChanged();
        return;
    }

    addStoredWaypoint(entry);

#if ENABLE_WAYPOINT_PERSISTENCE
    markWaypointStoreUnsaved();
#endif
    notifyChanged();
}

bool WaypointStore::purgeExpired(uint32_t now)
{
    if (now == 0)
        now = getTime();
    if (now == 0)
        return false;

    bool changed = false;
    for (auto it = waypoints.begin(); it != waypoints.end();) {
        if (!isExpired(*it, now)) {
            ++it;
            continue;
        }

        if (geofenceModule)
            geofenceModule->onWaypointReceived(it->waypoint, it->creatorNodeNum);

        it = waypoints.erase(it);
        changed = true;
    }

#if ENABLE_WAYPOINT_PERSISTENCE
    if (changed)
        markWaypointStoreUnsaved();
#endif
    if (changed)
        notifyChanged();

    return changed;
}

void WaypointStore::saveToFlash()
{
    purgeExpired();

#if ENABLE_WAYPOINT_PERSISTENCE && defined(FSCom)
    spiLock->lock();
    FSCom.mkdir("/");
    spiLock->unlock();

    SafeFile f(filename.c_str(), false);

    spiLock->lock();
    const uint8_t version = WAYPOINT_STORE_VERSION;
    uint8_t count = (uint8_t)waypoints.size();
    if (count > WAYPOINT_HISTORY_LIMIT)
        count = WAYPOINT_HISTORY_LIMIT;

    f.write(&version, 1);
    f.write(&count, 1);

    for (uint8_t i = 0; i < count; ++i) {
        StoredWaypointRecord rec = {};
        rec.creatorNodeNum = waypoints[i].creatorNodeNum;
        rec.receivedTime = waypoints[i].receivedTime;
        rec.payloadLength = encodeWaypointPayload(waypoints[i].waypoint, rec.payload, sizeof(rec.payload));
        f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
    }
    spiLock->unlock();
    f.close();
#endif

#if ENABLE_WAYPOINT_PERSISTENCE
    g_waypointStoreHasUnsavedChanges = false;
    g_lastWaypointAutoSaveMs = millis();
#endif
}

void WaypointStore::loadFromFlash()
{
    std::deque<StoredWaypoint>().swap(waypoints);

#if ENABLE_WAYPOINT_PERSISTENCE && defined(FSCom)
    concurrency::LockGuard guard(spiLock);

    if (FSCom.exists(filename.c_str())) {
        auto f = FSCom.open(filename.c_str(), FILE_O_READ);
        if (f) {
            uint8_t version = 0;
            uint8_t count = 0;
            f.readBytes(reinterpret_cast<char *>(&version), 1);
            f.readBytes(reinterpret_cast<char *>(&count), 1);

            if (version != 1 && version != WAYPOINT_STORE_VERSION) {
                LOG_WARN("WaypointStore version mismatch (%u)", version);
                f.close();
            } else {
                if (count > WAYPOINT_HISTORY_LIMIT)
                    count = WAYPOINT_HISTORY_LIMIT;

                for (uint8_t i = 0; i < count; ++i) {
                    StoredWaypoint entry;
                    if (version == 1) {
                        StoredWaypointRecordV1 rec = {};
                        if (f.readBytes(reinterpret_cast<char *>(&rec), sizeof(rec)) != sizeof(rec))
                            break;
                        if (rec.payloadLength == 0 || rec.payloadLength > sizeof(rec.payload)) {
                            LOG_WARN("WaypointStore skipping corrupt record %u", i);
                            continue;
                        }
                        if (!decodeWaypointPayload(rec.payload, rec.payloadLength, entry.waypoint))
                            continue;
                        entry.receivedTime = rec.receivedTime;
                        entry.creatorNodeNum = 0;
                    } else {
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
                    }

                    if (entry.creatorNodeNum == 0 && version == 1) {
                        // Legacy records had no creator. Keep them visible, but they won't geofence-track
                        // until refreshed from the mesh with creator metadata.
                    }

                    if (isExpired(entry.waypoint))
                        continue;
                    waypoints.push_back(entry);
                }
                f.close();
            }
        }
    }
#endif

#if ENABLE_WAYPOINT_PERSISTENCE
    g_waypointStoreHasUnsavedChanges = false;
    g_lastWaypointAutoSaveMs = millis();
#endif
}

void WaypointStore::clearAllWaypoints()
{
    const bool hadWaypoints = !waypoints.empty();
    std::deque<StoredWaypoint>().swap(waypoints);

#if ENABLE_WAYPOINT_PERSISTENCE && defined(FSCom)
    SafeFile f(filename.c_str(), false);
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
    g_lastWaypointAutoSaveMs = millis();
#endif

    if (hadWaypoints)
        notifyChanged();
}

void WaypointStore::replayToGeofence() const
{
    if (!geofenceModule)
        return;

    const uint32_t now = getTime();
    for (auto it = waypoints.rbegin(); it != waypoints.rend(); ++it) {
        if (!isExpired(*it, now))
            geofenceModule->onWaypointReceived(it->waypoint, it->creatorNodeNum);
    }
}

#if ENABLE_WAYPOINT_PERSISTENCE
void waypointStoreAutosaveTick()
{
    if (!g_waypointStoreHasUnsavedChanges) {
        if (g_lastWaypointAutoSaveMs == 0)
            g_lastWaypointAutoSaveMs = millis();
        return;
    }

    if (g_lastWaypointAutoSaveMs == 0) {
        g_lastWaypointAutoSaveMs = millis();
        return;
    }

    Throttle::execute(&g_lastWaypointAutoSaveMs, autosaveIntervalMs(), persistWaypointStore);
}
#endif

#endif
