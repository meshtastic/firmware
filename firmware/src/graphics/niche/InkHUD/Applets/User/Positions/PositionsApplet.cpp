#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./PositionsApplet.h"

using namespace NicheGraphics;

void InkHUD::PositionsApplet::onRender()
{
    // Draw the usual map applet first
    MapApplet::onRender();

    // Draw our latest "node of interest" as a special marker
    // -------------------------------------------------------
    // We might be rendering because we got a position packet from them
    // We might be rendering because our own position updated
    // Either way, we still highlight which node most recently sent us a position packet
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(lastFrom);
    if (node && nodeDB->hasValidPosition(node) && enoughMarkers())
        drawLabeledMarker(node);
}

// Determine if we need to redraw the map, when we receive a new position packet
ProcessMessage InkHUD::PositionsApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // If applet is not active, we shouldn't be handling any data
    // It's good practice for all applets to implement an early return like this
    // for PositionsApplet, this is **required** - it's where we're handling active vs deactive
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Try decode a position from the packet
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
                lat = position.latitude_i * 1e-7; // Convert from Meshtastic's internal  int32_t format
                lng = position.longitude_i * 1e-7;
            }
        }
    }

    // Skip if we didn't get a valid position
    if (!hasPosition)
        return ProcessMessage::CONTINUE;

    bool hasHopsAway = (mp.hop_start != 0 && mp.hop_limit <= mp.hop_start); // From NodeDB::updateFrom
    uint8_t hopsAway = mp.hop_start - mp.hop_limit;

    // Determine if the position packet would change anything on-screen
    // -----------------------------------------------------------------

    bool somethingChanged = false;

    // If our own position
    if (isFromUs(&mp)) {
        // We get frequent position updates from connected phone
        // Only update if we're travelled some distance, for rate limiting
        // Todo: smarter detection of position changes
        if (GeoCoord::latLongToMeter(ourLastLat, ourLastLng, lat, lng) > 50) {
            somethingChanged = true;
            ourLastLat = lat;
            ourLastLng = lng;
        }
    }

    // If someone else's position
    else {
        // Check if this position is from someone different than our previous position packet
        if (mp.from != lastFrom) {
            somethingChanged = true;
            lastFrom = mp.from;
            lastLat = lat;
            lastLng = lng;
            lastHopsAway = hopsAway;
        }

        // Same sender: check if position changed
        // Todo: smarter detection of position changes
        else if (GeoCoord::latLongToMeter(lastLat, lastLng, lat, lng) > 10) {
            somethingChanged = true;
            lastLat = lat;
            lastLng = lng;
        }

        // Same sender, same position: check if hops changed
        // Only pay attention if the hopsAway value is valid
        else if (hasHopsAway && (hopsAway != lastHopsAway)) {
            somethingChanged = true;
            lastHopsAway = hopsAway;
        }
    }

    // Decision reached
    // -----------------

    if (somethingChanged) {
        requestAutoshow(); // Todo: only request this in some situations?
        requestUpdate();
    }

    return ProcessMessage::CONTINUE;
}

#endif
