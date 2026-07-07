#include "GeofenceModule.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT

#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "mesh/NodeDB.h"
#include <algorithm>
#include <cstring>

#if HAS_SCREEN
#include "PowerFSM.h"
#include "graphics/Screen.h"
#include "main.h" // screen
#endif

#include "modules/ExternalNotificationModule.h"

GeofenceModule *geofenceModule;

// Keep the in-memory footprint bounded. A mesh realistically has only a handful of active
// geofencing waypoints; the crossing-state map grows with (waypoints x nodes).
static constexpr size_t GEOFENCE_MAX_WAYPOINTS = 32;
static constexpr size_t GEOFENCE_MAX_CROSSING = 256;

GeofenceModule::GeofenceModule()
{
    geofences.reserve(GEOFENCE_MAX_WAYPOINTS);
    crossingInside.reserve(GEOFENCE_MAX_CROSSING);
}

// --- Pure helpers -----------------------------------------------------------------------------

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

GeofenceModule::Crossing GeofenceModule::classifyTrackedUpdate(bool hasTrackedState, bool trackedInside, bool hasPreviousPosition,
                                                               bool previousInside, bool isInside, bool notifyOnEnter,
                                                               bool notifyOnExit)
{
    if (hasTrackedState)
        return classify(false, trackedInside, isInside, notifyOnEnter, notifyOnExit);
    if (hasPreviousPosition)
        return classify(false, previousInside, isInside, notifyOnEnter, notifyOnExit);
    return classify(true, false, isInside, notifyOnEnter, notifyOnExit);
}

// --- Store management -------------------------------------------------------------------------

void GeofenceModule::removeGeofence(uint32_t waypointId, NodeNum creatorNodeNum)
{
    geofences.erase(std::remove_if(geofences.begin(), geofences.end(),
                                   [waypointId, creatorNodeNum](const Geofence &geofence) {
                                       return geofence.id == waypointId &&
                                              (creatorNodeNum == 0 || geofence.creatorNodeNum == creatorNodeNum);
                                   }),
                    geofences.end());
    crossingInside.erase(
        std::remove_if(crossingInside.begin(), crossingInside.end(),
                       [waypointId](const CrossingState &state) { return (uint32_t)(state.key >> 32) == waypointId; }),
        crossingInside.end());
}

void GeofenceModule::purgeExpired(uint32_t now)
{
    if (now == 0)
        return; // no trustworthy clock; can't judge expiry
    for (size_t i = 0; i < geofences.size();) {
        if (geofences[i].expire != 0 && geofences[i].expire <= now) {
            uint32_t id = geofences[i].id;
            geofences.erase(geofences.begin() + i);
            crossingInside.erase(std::remove_if(crossingInside.begin(), crossingInside.end(),
                                                [id](const CrossingState &state) { return (uint32_t)(state.key >> 32) == id; }),
                                 crossingInside.end());
        } else {
            i++;
        }
    }
}

GeofenceModule::CrossingState *GeofenceModule::findCrossingState(uint64_t key)
{
    for (auto &state : crossingInside) {
        if (state.key == key)
            return &state;
    }

    return nullptr;
}

bool GeofenceModule::shouldTrack(const meshtastic_Waypoint &wp, uint32_t now)
{
    // Must carry a geofence and ask for at least one notification.
    if (!hasGeofence(wp))
        return false;
    if (!wp.notify_on_enter && !wp.notify_on_exit)
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

void GeofenceModule::onWaypointReceived(const meshtastic_Waypoint &wp, NodeNum creatorNodeNum)
{
    uint32_t now = getTime();
    purgeExpired(now);

    const NodeNum localNodeNum = nodeDB ? nodeDB->getNodeNum() : 0;

    // Drop anything we don't track (a plain pin, notifications turned off, an expired or deleted
    // waypoint, or a circle with no centre) from the store.
    if (!shouldTrack(wp, now) || creatorNodeNum == 0 || creatorNodeNum != localNodeNum) {
        removeGeofence(wp.id, creatorNodeNum);
        return;
    }

    Geofence *slot = nullptr;
    for (auto &g : geofences) {
        if (g.id == wp.id && g.creatorNodeNum == creatorNodeNum) {
            slot = &g;
            break;
        }
    }
    if (!slot) {
        if (geofences.size() >= GEOFENCE_MAX_WAYPOINTS) {
            LOG_WARN("Geofence store full (%u); ignoring waypoint 0x%08x", (unsigned)geofences.size(), wp.id);
            return;
        }
        geofences.push_back(Geofence{});
        slot = &geofences.back();
    }

    slot->id = wp.id;
    slot->creatorNodeNum = creatorNodeNum;
    slot->latitude_i = wp.latitude_i;
    slot->longitude_i = wp.longitude_i;
    slot->geofence_radius = wp.geofence_radius;
    slot->has_bounding_box = wp.has_bounding_box;
    slot->bounding_box = wp.bounding_box;
    slot->notify_on_enter = wp.notify_on_enter;
    slot->notify_on_exit = wp.notify_on_exit;
    slot->notify_favorites_only = wp.notify_favorites_only;
    slot->expire = wp.expire;
    strncpy(slot->name, wp.name, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';

    LOG_INFO("Geofence: tracking waypoint 0x%08x '%s' creator=0x%08x radius=%um box=%d enter=%d exit=%d favOnly=%d", wp.id,
             slot->name, (unsigned)creatorNodeNum, wp.geofence_radius, wp.has_bounding_box, wp.notify_on_enter, wp.notify_on_exit,
             wp.notify_favorites_only);
}

// --- Evaluation ------------------------------------------------------------------------------

void GeofenceModule::evaluatePosition(NodeNum node, const meshtastic_Position &p, bool hasPreviousPosition, int32_t previousLat_i,
                                      int32_t previousLon_i)
{
    if (geofences.empty())
        return;
    if (!p.has_latitude_i || !p.has_longitude_i)
        return;
    if (p.latitude_i == 0 && p.longitude_i == 0)
        return; // treat the null island as "no fix"
    if (node == nodeDB->getNodeNum())
        return; // judge other nodes' positions only (per design#114)

    purgeExpired(getTime());

    const int32_t lat = p.latitude_i;
    const int32_t lon = p.longitude_i;
    bool favoriteResolved = false;
    bool isFavorite = false;

    for (auto &g : geofences) {
        const bool isInside =
            insideAny(lat, lon, g.latitude_i, g.longitude_i, g.geofence_radius, g.has_bounding_box, g.bounding_box);
        const uint64_t key = crossingKey(g.id, node);
        CrossingState *state = findCrossingState(key);
        const bool hasTrackedState = (state != nullptr);
        const bool previousInside = hasPreviousPosition ? insideAny(previousLat_i, previousLon_i, g.latitude_i, g.longitude_i,
                                                                    g.geofence_radius, g.has_bounding_box, g.bounding_box)
                                                        : false;

        const Crossing crossing =
            classifyTrackedUpdate(hasTrackedState, hasTrackedState ? state->inside : false, hasPreviousPosition, previousInside,
                                  isInside, g.notify_on_enter, g.notify_on_exit);

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

        if (g.notify_favorites_only) {
            if (!favoriteResolved) {
                isFavorite = nodeDB->isFavorite(node);
                favoriteResolved = true;
            }
            if (!isFavorite)
                continue;
        }

        notify(g, node, crossing == Crossing::Enter);
    }
}

void GeofenceModule::notify(const Geofence &g, NodeNum node, bool entered)
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

    LOG_INFO("Geofence: %s %s '%s'", who, entered ? "entered" : "left", g.name);

#if HAS_SCREEN
    if (screen)
        powerFSM.trigger(EVENT_RECEIVED_MSG); // wake the screen so the banner is seen
#endif

    GeofenceNotificationEvent event;
    strncpy(event.nodeName, who, sizeof(event.nodeName) - 1);
    event.nodeName[sizeof(event.nodeName) - 1] = '\0';
    event.entered = entered;
    strncpy(event.geofenceName, g.name, sizeof(event.geofenceName) - 1);
    event.geofenceName[sizeof(event.geofenceName) - 1] = '\0';
    notifyObservers(&event);

#if HAS_SCREEN && !defined(MESHTASTIC_INCLUDE_INKHUD)
    if (screen) {
        char banner[120];
        snprintf(banner, sizeof(banner), "%s %s %s", who, entered ? "IN" : "OUT", g.name);
        screen->showSimpleBanner(banner, 5000);
    }
#endif

    if (externalNotificationModule)
        externalNotificationModule->startNotification();
}

#endif // !MESHTASTIC_EXCLUDE_WAYPOINT
