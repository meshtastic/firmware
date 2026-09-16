#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "Observer.h"
#include "WaypointStore.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <vector>

struct GeofenceNotificationEvent {
    uint32_t waypointId = 0;
    char nodeName[sizeof(meshtastic_User::long_name)] = {};
    bool entered = false;
    char geofenceName[sizeof(meshtastic_Waypoint::name)] = {};
};

// Tracks other nodes crossing waypoint geofences and emits enter/exit notifications on the waypoint creator's device.
class GeofenceModule : public Observable<const GeofenceNotificationEvent *>
{
  public:
    GeofenceModule();

    enum class Crossing { None, Enter, Exit };

    static bool insideRadius(int32_t ptLat_i, int32_t ptLon_i, int32_t ctrLat_i, int32_t ctrLon_i, uint32_t radiusMeters);

    static bool insideBox(int32_t ptLat_i, int32_t ptLon_i, const meshtastic_BoundingBox &box);

    static bool insideAny(int32_t ptLat_i, int32_t ptLon_i, int32_t ctrLat_i, int32_t ctrLon_i, uint32_t radiusMeters,
                          bool hasBox, const meshtastic_BoundingBox &box);

    static bool inside(const meshtastic_Waypoint &wp, int32_t ptLat_i, int32_t ptLon_i);

    static bool hasGeofence(const meshtastic_Waypoint &wp);

    // Box-only geofences do not require a waypoint center because their corners are absolute.
    static bool shouldTrack(const meshtastic_Waypoint &wp, uint8_t notificationPreferences, uint32_t now);

    // The first sighting establishes a baseline without notifying.
    static Crossing classify(bool firstSighting, bool wasInside, bool isInside, bool notifyOnEnter, bool notifyOnExit);

    void evaluatePosition(NodeNum node, const meshtastic_Position &p);

  private:
    struct CrossingState {
        uint64_t key;
        bool inside;
    };

    static uint64_t crossingKey(uint32_t waypointId, NodeNum node) { return ((uint64_t)waypointId << 32) | node; }

    CrossingState *findCrossingState(uint64_t key);
    void notify(const meshtastic_Waypoint &wp, NodeNum node, bool entered);
    int onWaypointStoreChanged(const WaypointStore *store);

    // Bounded (waypointId, nodeNum) state; new pairs are skipped until an old waypoint frees space.
    std::vector<CrossingState> crossingInside;
    CallbackObserver<GeofenceModule, const WaypointStore *> waypointStoreObserver =
        CallbackObserver<GeofenceModule, const WaypointStore *>(this, &GeofenceModule::onWaypointStoreChanged);
};

extern GeofenceModule *geofenceModule;

#endif
