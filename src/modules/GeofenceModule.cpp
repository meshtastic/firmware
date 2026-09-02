#include "GeofenceModule.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "WaypointStore.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "mesh/NodeDB.h"
#include <cstring>

#if HAS_SCREEN
#include "PowerFSM.h"
#include "graphics/Screen.h"
#include "main.h" // screen
#endif

#include "modules/ExternalNotificationModule.h"

GeofenceModule *geofenceModule;

static constexpr size_t GEOFENCE_MAX_CROSSING = 256;

GeofenceModule::GeofenceModule()
{
    crossingInside.reserve(GEOFENCE_MAX_CROSSING);
    waypointStoreObserver.observe(&waypointStore);
}

bool GeofenceModule::insideRadius(int32_t ptLat_i, int32_t ptLon_i, int32_t ctrLat_i, int32_t ctrLon_i, uint32_t radiusMeters)
{
    if (radiusMeters == 0)
        return false;
    float meters = GeoCoord::latLongToMeter((double)ptLat_i * 1e-7, (double)ptLon_i * 1e-7, (double)ctrLat_i * 1e-7,
                                            (double)ctrLon_i * 1e-7);
    return meters <= (float)radiusMeters;
}

bool GeofenceModule::insideBox(int32_t ptLat_i, int32_t ptLon_i, const meshtastic_BoundingBox &box)
{
    return ptLat_i >= box.latitude_south_i && ptLat_i <= box.latitude_north_i && ptLon_i >= box.longitude_west_i &&
           ptLon_i <= box.longitude_east_i;
}

bool GeofenceModule::insideAny(int32_t ptLat_i, int32_t ptLon_i, int32_t ctrLat_i, int32_t ctrLon_i, uint32_t radiusMeters,
                               bool hasBox, const meshtastic_BoundingBox &box)
{
    if (insideRadius(ptLat_i, ptLon_i, ctrLat_i, ctrLon_i, radiusMeters))
        return true;
    if (hasBox && insideBox(ptLat_i, ptLon_i, box))
        return true;
    return false;
}

bool GeofenceModule::inside(const meshtastic_Waypoint &wp, int32_t ptLat_i, int32_t ptLon_i)
{
    return insideAny(ptLat_i, ptLon_i, wp.latitude_i, wp.longitude_i, wp.geofence_radius, wp.has_bounding_box, wp.bounding_box);
}

bool GeofenceModule::hasGeofence(const meshtastic_Waypoint &wp)
{
    return wp.geofence_radius > 0 || wp.has_bounding_box;
}

GeofenceModule::Crossing GeofenceModule::classify(bool firstSighting, bool wasInside, bool isInside, bool notifyOnEnter,
                                                  bool notifyOnExit)
{
    if (firstSighting)
        return Crossing::None; // baseline only
    if (wasInside == isInside)
        return Crossing::None; // no transition
    if (isInside)
        return notifyOnEnter ? Crossing::Enter : Crossing::None;
    return notifyOnExit ? Crossing::Exit : Crossing::None;
}

GeofenceModule::CrossingState *GeofenceModule::findCrossingState(uint64_t key)
{
    for (auto &state : crossingInside) {
        if (state.key == key)
            return &state;
    }

    return nullptr;
}

bool GeofenceModule::shouldTrack(const meshtastic_Waypoint &wp, uint8_t notificationPreferences, uint32_t now)
{
    if (!hasGeofence(wp))
        return false;
    if ((notificationPreferences & (WAYPOINT_NOTIFY_ENTER | WAYPOINT_NOTIFY_EXIT)) == 0)
        return false;
    // Only the circle is centred on the waypoint; the bounding box carries its own absolute
    // corners, so a box-only geofence does not need a latitude/longitude pin.
    if (wp.geofence_radius > 0 && !(wp.has_latitude_i && wp.has_longitude_i))
        return false;
    // Expired/deleted? (now == 0 means we have no trustworthy clock, so treat it as still live.)
    if (now != 0 && wp.expire != 0 && wp.expire <= now)
        return false;
    return true;
}

int GeofenceModule::onWaypointStoreChanged(const WaypointStore *store)
{
    (void)store;
    crossingInside.clear();
    return 0;
}

void GeofenceModule::evaluatePosition(NodeNum node, const meshtastic_Position &p)
{
    if (waypointStore.getWaypoints().empty())
        return;
    if (!p.has_latitude_i || !p.has_longitude_i)
        return;
    if (p.latitude_i == 0 && p.longitude_i == 0)
        return; // treat the null island as "no fix"
    if (node == nodeDB->getNodeNum())
        return; // judge other nodes' positions only (per design#114)

    const int32_t lat = p.latitude_i;
    const int32_t lon = p.longitude_i;
    const uint32_t now = getTime();
    bool favoriteResolved = false;
    bool isFavorite = false;

    for (const StoredWaypoint &entry : waypointStore.getWaypoints()) {
        const meshtastic_Waypoint &wp = entry.waypoint;
        if (!shouldTrack(wp, entry.notificationPreferences, now))
            continue;

        const bool isInside = inside(wp, lat, lon);
        const uint64_t key = crossingKey(wp.id, node);
        CrossingState *state = findCrossingState(key);
        const bool hasTrackedState = (state != nullptr);
        const Crossing crossing =
            classify(!hasTrackedState, hasTrackedState ? state->inside : false, isInside,
                     entry.notificationEnabled(WAYPOINT_NOTIFY_ENTER), entry.notificationEnabled(WAYPOINT_NOTIFY_EXIT));

        // Record/baseline the current state (bounded - drop new pairs once the map is full).
        if (!hasTrackedState) {
            if (crossingInside.size() < GEOFENCE_MAX_CROSSING) {
                crossingInside.push_back(CrossingState{key, isInside});
            } else {
                static bool warnedCrossingFull = false;
                if (!warnedCrossingFull) {
                    LOG_WARN("Geofence crossing-state full (%u); new (waypoint,node) pairs will not alert until space frees",
                             (unsigned)GEOFENCE_MAX_CROSSING);
                    warnedCrossingFull = true;
                }
            }
        } else {
            state->inside = isInside;
        }

        if (crossing == Crossing::None)
            continue;

        if (entry.notificationEnabled(WAYPOINT_NOTIFY_FAVORITES_ONLY)) {
            if (!favoriteResolved) {
                isFavorite = nodeDB->isFavorite(node);
                favoriteResolved = true;
            }
            if (!isFavorite)
                continue;
        }

        notify(wp, node, crossing == Crossing::Enter);
    }
}

void GeofenceModule::notify(const meshtastic_Waypoint &wp, NodeNum node, bool entered)
{
    // Resolve a display name for the crossing node.
    char who[40];
    const meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(node);
    if (info && info->long_name[0]) {
        strncpy(who, info->long_name, sizeof(who) - 1);
        who[sizeof(who) - 1] = '\0';
    } else if (info && info->short_name[0]) {
        strncpy(who, info->short_name, sizeof(who) - 1);
        who[sizeof(who) - 1] = '\0';
    } else {
        snprintf(who, sizeof(who), "!%08x", (unsigned)node);
    }

    LOG_INFO("Geofence: %s %s '%s'", who, entered ? "entered" : "left", wp.name);

#if HAS_SCREEN
    if (screen)
        powerFSM.trigger(EVENT_RECEIVED_MSG); // wake the screen so the banner is seen
#endif

    GeofenceNotificationEvent event;
    event.waypointId = wp.id;
    strncpy(event.nodeName, who, sizeof(event.nodeName) - 1);
    event.nodeName[sizeof(event.nodeName) - 1] = '\0';
    event.entered = entered;
    strncpy(event.geofenceName, wp.name, sizeof(event.geofenceName) - 1);
    event.geofenceName[sizeof(event.geofenceName) - 1] = '\0';
    notifyObservers(&event);

#if HAS_SCREEN && !defined(MESHTASTIC_INCLUDE_INKHUD)
    if (screen) {
        char banner[120];
        snprintf(banner, sizeof(banner), "%s %s %s", who, entered ? "IN" : "OUT", wp.name);
        screen->showSimpleBanner(banner, 5000);
    }
#endif

    if (externalNotificationModule)
        externalNotificationModule->startNotification();
}

#endif
