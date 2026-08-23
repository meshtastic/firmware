#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#ifndef ENABLE_WAYPOINT_PERSISTENCE
#define ENABLE_WAYPOINT_PERSISTENCE 1
#endif

#ifndef WAYPOINT_HISTORY_LIMIT
#define WAYPOINT_HISTORY_LIMIT 10
#endif

#include "Observer.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <deque>
#include <string>

struct StoredWaypoint {
    meshtastic_Waypoint waypoint = meshtastic_Waypoint_init_zero;
    uint32_t receivedTime = 0;
    NodeNum creatorNodeNum = 0;
};

class WaypointStore : public Observable<const WaypointStore *>
{
  public:
    explicit WaypointStore(const std::string &label);

    bool addFromPacket(const meshtastic_MeshPacket &packet, StoredWaypoint *stored = nullptr);
    bool purgeExpired(uint32_t now = 0);
    bool removeWaypoint(uint32_t id);

    const std::deque<StoredWaypoint> &getWaypoints() const { return waypoints; }

    void saveToFlash();
    void loadFromFlash();
    void clearAllWaypoints();

    static bool isExpired(const meshtastic_Waypoint &wp, uint32_t now = 0);
    static bool isExpired(const StoredWaypoint &entry, uint32_t now = 0);

  private:
    void addStoredWaypoint(const StoredWaypoint &entry);
    bool removeWaypointById(uint32_t id);
    void untrackGeofence(uint32_t id) const;
    void replayToGeofence() const;
    void notifyChanged();

    std::deque<StoredWaypoint> waypoints;
    std::string filename;
};

#if ENABLE_WAYPOINT_PERSISTENCE
void waypointStoreAutosaveTick();
#endif

extern WaypointStore waypointStore;

#endif
