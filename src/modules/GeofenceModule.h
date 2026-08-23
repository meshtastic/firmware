#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "Observer.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <vector>

struct GeofenceNotificationEvent {
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
    static bool shouldTrack(const meshtastic_Waypoint &wp, uint32_t now);

    // The first sighting establishes a baseline without notifying.
    static Crossing classify(bool firstSighting, bool wasInside, bool isInside, bool notifyOnEnter, bool notifyOnExit);

    // Tracked state wins; otherwise a supplied previous position provides the baseline.
    static Crossing classifyTrackedUpdate(bool hasTrackedState, bool trackedInside, bool hasPreviousPosition, bool previousInside,
                                          bool isInside, bool notifyOnEnter, bool notifyOnExit);

    void onWaypointReceived(const meshtastic_Waypoint &wp, NodeNum creatorNodeNum = 0);

    void evaluatePosition(NodeNum node, const meshtastic_Position &p, bool hasPreviousPosition = false, int32_t previousLat_i = 0,
                          int32_t previousLon_i = 0);

  private:
    struct Geofence {
        uint32_t id;
        NodeNum creatorNodeNum;
        int32_t latitude_i;
        int32_t longitude_i;
        uint32_t geofence_radius;
        bool has_bounding_box;
        meshtastic_BoundingBox bounding_box;
        bool notify_on_enter;
        bool notify_on_exit;
        bool notify_favorites_only;
        uint32_t expire;
        char name[sizeof(meshtastic_Waypoint::name)];
    };

    struct CrossingState {
        uint64_t key;
        bool inside;
    };

    static uint64_t crossingKey(uint32_t waypointId, NodeNum node) { return ((uint64_t)waypointId << 32) | node; }

    void purgeExpired(uint32_t now);
    void removeGeofence(uint32_t waypointId, NodeNum creatorNodeNum = 0);
    CrossingState *findCrossingState(uint64_t key);
    void notify(const Geofence &g, NodeNum node, bool entered);

    std::vector<Geofence> geofences;
    // Bounded (waypointId, nodeNum) state; new pairs are skipped until an old waypoint frees space.
    std::vector<CrossingState> crossingInside;
};

extern GeofenceModule *geofenceModule;

#endif // !MESHTASTIC_EXCLUDE_WAYPOINT
