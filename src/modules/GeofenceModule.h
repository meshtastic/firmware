#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "Observer.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <vector>

struct GeofenceNotificationEvent {
    char nodeName[40] = {};
    bool entered = false;
    char geofenceName[sizeof(meshtastic_Waypoint::name)] = {};
};

/**
 * GeofenceModule - raises on-device enter/exit alerts when a tracked node crosses the geofence
 * carried by a Waypoint (Waypoint.geofence_radius and/or Waypoint.bounding_box).
 *
 * Geofences travel with the waypoint across the mesh; there is no app-local geofence model. This
 * engine is UI-independent:
 *   - WaypointModule feeds it geofencing waypoints as they arrive (onWaypointReceived).
 *   - PositionModule feeds it each received (other-node) position (evaluatePosition).
 *
 * Behaviour matches the client spec (meshtastic/design#114):
 *   - Inside test: within the circle (distance <= geofence_radius) OR inside the bounding box.
 *   - Per-pair (waypointId, nodeNum) inside/outside state. The FIRST sighting of a pair only sets a
 *     baseline (no notification), unless the caller can supply the node's immediately previous
 *     cached position, in which case that previous sample is used to infer the transition.
 *   - Only a genuine inside<->outside transition notifies: enter if notify_on_enter, exit if
 *     notify_on_exit. notify_favorites_only gates alerts to the receiver's own favourites.
 *   - Only the waypoint creator's node should receive enter/exit alerts for that waypoint.
 *   - All state is in-memory; it need not survive a restart (the baseline rule handles relaunch).
 *
 * Crossing is judged against OTHER nodes' reported positions, not this device's own location.
 */
class GeofenceModule : public Observable<const GeofenceNotificationEvent *>
{
  public:
    GeofenceModule();

    enum class Crossing { None, Enter, Exit };

    // --- Pure, side-effect-free helpers (unit-tested without device globals) ---

    /// True if the point (degrees x 1e-7) is within radiusMeters of the centre. radius 0 => false.
    static bool insideRadius(int32_t ptLat_i, int32_t ptLon_i, int32_t ctrLat_i, int32_t ctrLon_i, uint32_t radiusMeters);

    /// True if the point (degrees x 1e-7) is inside the axis-aligned box (edges inclusive).
    static bool insideBox(int32_t ptLat_i, int32_t ptLon_i, const meshtastic_BoundingBox &box);

    /// Combined inside test: inside the circle OR (if present) inside the box. Either shape counts.
    static bool insideAny(int32_t ptLat_i, int32_t ptLon_i, int32_t ctrLat_i, int32_t ctrLon_i, uint32_t radiusMeters,
                          bool hasBox, const meshtastic_BoundingBox &box);

    /// Convenience inside test against a whole waypoint (used by callers and tests).
    static bool inside(const meshtastic_Waypoint &wp, int32_t ptLat_i, int32_t ptLon_i);

    /// True if the waypoint carries any geofence (a circle and/or a box).
    static bool hasGeofence(const meshtastic_Waypoint &wp);

    /// True if this waypoint should be tracked for alerts: it has a geofence, asks for an enter or
    /// exit notification, isn't expired (now == 0 => treat as live), and - when it has a circle -
    /// carries a centre. A box-only geofence needs no centre (its corners are absolute).
    static bool shouldTrack(const meshtastic_Waypoint &wp, uint32_t now);

    /// Decide what (if anything) to notify, given prior/current inside state and the notify flags.
    /// The first sighting of a pair only establishes a baseline and returns None.
    static Crossing classify(bool firstSighting, bool wasInside, bool isInside, bool notifyOnEnter, bool notifyOnExit);

    /// Decide what to notify for an update when tracked state may not exist yet. If we already
    /// track the pair, trackedInside wins. Otherwise, a supplied previous sample becomes the
    /// baseline; without either, the update only establishes baseline state.
    static Crossing classifyTrackedUpdate(bool hasTrackedState, bool trackedInside, bool hasPreviousPosition, bool previousInside,
                                          bool isInside, bool notifyOnEnter, bool notifyOnExit);

    // --- Engine API (device side) ---

    /// Ingest a waypoint (from WaypointModule): upsert or remove its geofence in the in-memory store.
    void onWaypointReceived(const meshtastic_Waypoint &wp, NodeNum creatorNodeNum = 0);

    /// Evaluate a received node position (from PositionModule) against all known geofences.
    void evaluatePosition(NodeNum node, const meshtastic_Position &p, bool hasPreviousPosition = false, int32_t previousLat_i = 0,
                          int32_t previousLon_i = 0);

  private:
    // Trimmed copy of a geofencing waypoint we are tracking (name kept for the alert text).
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
    // (waypointId, nodeNum) -> last known inside state. Bounded: once it is full, new pairs are
    // dropped (they stay in the "first sighting" baseline state and won't alert) until a tracked
    // waypoint is removed/expires and frees space.
    std::vector<CrossingState> crossingInside;
};

extern GeofenceModule *geofenceModule;

#endif // !MESHTASTIC_EXCLUDE_WAYPOINT
