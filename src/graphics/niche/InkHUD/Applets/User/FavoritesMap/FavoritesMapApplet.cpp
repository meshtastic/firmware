#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./FavoritesMapApplet.h"
#include "NodeDB.h"

using namespace NicheGraphics;

bool InkHUD::FavoritesMapApplet::shouldDrawNode(meshtastic_NodeInfoLite *node)
{
    // Keep our own node available as map anchor/center; all others must be favorited.
    return node && (node->num == nodeDB->getNodeNum() || node->is_favorite);
}

void InkHUD::FavoritesMapApplet::onRender(bool full)
{
    // Custom empty state text for favorites-only map.
    if (!enoughMarkers()) {
        printAt(X(0.5), Y(0.5) - (getFont().lineHeight() / 2), "Favorite node position", CENTER, MIDDLE);
        printAt(X(0.5), Y(0.5) + (getFont().lineHeight() / 2), "will appear here", CENTER, MIDDLE);
        return;
    }

    // Draw the usual map applet first.
    MapApplet::onRender(full);

    // Draw our latest "node of interest" as a special marker.
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(lastFrom);
    if (node && node->is_favorite && nodeDB->hasValidPosition(node) && enoughMarkers())
        drawLabeledMarker(node);
}

// Determine if we need to redraw the map, when we receive a new position packet.
ProcessMessage InkHUD::FavoritesMapApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // If applet is not active, we shouldn't be handling any data.
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Try decode a position from the packet.
    bool hasPosition = false;
    float lat;
    float lng;
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.portnum == meshtastic_PortNum_POSITION_APP) {
        meshtastic_Position position = meshtastic_Position_init_default;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &position)) {
            if (position.has_latitude_i && position.has_longitude_i         // Actually has position
                && (position.latitude_i != 0 || position.longitude_i != 0)) // Position isn't "null island"
            {
                hasPosition = true;
                lat = position.latitude_i * 1e-7; // Convert from Meshtastic's internal int32_t format
                lng = position.longitude_i * 1e-7;
            }
        }
    }

    // Skip if we didn't get a valid position.
    if (!hasPosition)
        return ProcessMessage::CONTINUE;

    const int8_t hopsAway = getHopsAway(mp);
    const bool hasHopsAway = hopsAway >= 0;

    // Determine if the position packet would change anything on-screen.
    bool somethingChanged = false;

    // If our own position.
    if (isFromUs(&mp)) {
        // Ignore tiny local movement to reduce update spam.
        if (GeoCoord::latLongToMeter(ourLastLat, ourLastLng, lat, lng) > 50) {
            somethingChanged = true;
            ourLastLat = lat;
            ourLastLng = lng;
        }
    } else {
        // For non-local packets, this applet only reacts to favorited nodes.
        meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(mp.from);
        if (!sender || !sender->is_favorite)
            return ProcessMessage::CONTINUE;

        // Check if this position is from someone different than our previous position packet.
        if (mp.from != lastFrom) {
            somethingChanged = true;
            lastFrom = mp.from;
            lastLat = lat;
            lastLng = lng;
            lastHopsAway = hopsAway;
        }

        // Same sender: check if position changed.
        else if (GeoCoord::latLongToMeter(lastLat, lastLng, lat, lng) > 10) {
            somethingChanged = true;
            lastLat = lat;
            lastLng = lng;
        }

        // Same sender, same position: check if hops changed.
        else if (hasHopsAway && (hopsAway != lastHopsAway)) {
            somethingChanged = true;
            lastHopsAway = hopsAway;
        }
    }

    if (somethingChanged) {
        requestAutoshow();
        requestUpdate();
    }

    return ProcessMessage::CONTINUE;
}

#endif
