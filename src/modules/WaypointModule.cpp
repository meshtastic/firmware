#include "WaypointModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#if HAS_SCREEN
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "main.h"
#endif

WaypointModule *waypointModule;

ProcessMessage WaypointModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto &p = mp.decoded;
    LOG_INFO("Received waypoint msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif
    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_waypoint = mp;
    devicestate.has_rx_waypoint = true;

    powerFSM.trigger(EVENT_RECEIVED_MSG);

#if HAS_SCREEN

    UIFrameEvent e;

    // New or updated waypoint: focus on this frame next time Screen::setFrames runs
    if (shouldDraw()) {
        requestFocus();
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    }

    // Deleting an old waypoint: remove the frame quietly, don't change frame position if possible
    else
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND;

    notifyObservers(&e);

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

#if HAS_SCREEN
bool WaypointModule::shouldDraw()
{
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    // If no waypoint to show
    if (!devicestate.has_rx_waypoint)
        return false;

    // Decode the message, to find the expiration time (is waypoint still valid)
    // This handles "deletion" as well as expiration
    meshtastic_Waypoint wp;
    memset(&wp, 0, sizeof(wp));
    if (pb_decode_from_bytes(devicestate.rx_waypoint.decoded.payload.bytes, devicestate.rx_waypoint.decoded.payload.size,
                             &meshtastic_Waypoint_msg, &wp)) {
        // Valid waypoint
        if (wp.expire > getTime())
            return devicestate.has_rx_waypoint = true;

        // Expired, or deleted
        else
            return devicestate.has_rx_waypoint = false;
    }

    // If decoding failed
    LOG_ERROR("Failed to decode waypoint");
    devicestate.has_rx_waypoint = false;
    return false;
#else
    return false;
#endif
}

/// Draw the last waypoint we received
void WaypointModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Prepare to draw
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // Handle inverted display
    // Unsure of expected behavior: for now, copy drawNodeInfo
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED)
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);

    // Decode the waypoint
    const meshtastic_MeshPacket &mp = devicestate.rx_waypoint;
    meshtastic_Waypoint wp;
    memset(&wp, 0, sizeof(wp));
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Waypoint_msg, &wp)) {
        // This *should* be caught by shouldDrawWaypoint, but we'll short-circuit here just in case
        display->drawStringMaxWidth(0 + x, 0 + y, x + display->getWidth(), "Couldn't decode waypoint");
        devicestate.has_rx_waypoint = false;
        return;
    }

    // Get timestamp info. Will pass as a field to drawColumns
    static char lastStr[20];
    screen->getTimeAgoStr(sinceReceived(&mp), lastStr, sizeof(lastStr));

    // Will contain distance information, passed as a field to drawColumns
    static char distStr[20];

    // Get our node, to use our own position
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());

    // Text fields to draw (left of compass)
    // Last element must be NULL. This signals the end of the char*[] to drawColumns
    const char *fields[] = {"Waypoint", lastStr, wp.name, distStr, NULL};

    // Dimensions / co-ordinates for the compass/circle
    int16_t compassX = 0, compassY = 0;
    uint16_t compassDiam = graphics::Screen::getCompassDiam(display->getWidth(), display->getHeight());

    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + display->getHeight() / 2;
    } else {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + FONT_HEIGHT_SMALL + (display->getHeight() - FONT_HEIGHT_SMALL) / 2;
    }

    // If our node has a position:
    if (ourNode && (nodeDB->hasValidPosition(ourNode) || screen->hasHeading())) {
        const meshtastic_PositionLite &op = ourNode->position;
        float myHeading;
        if (screen->hasHeading())
            myHeading = (screen->getHeading()) * PI / 180; // gotta convert compass degrees to Radians
        else
            myHeading = screen->estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
        screen->drawCompassNorth(display, compassX, compassY, myHeading);

        // Compass bearing to waypoint
        float bearingToOther =
            GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(wp.latitude_i), DegD(wp.longitude_i));
        // If the top of the compass is a static north then bearingToOther can be drawn on the compass directly
        // If the top of the compass is not a static north we need adjust bearingToOther based on heading
        if (!config.display.compass_north_top)
            bearingToOther -= myHeading;
        screen->drawNodeHeading(display, compassX, compassY, compassDiam, bearingToOther);

        float bearingToOtherDegrees = (bearingToOther < 0) ? bearingToOther + 2 * PI : bearingToOther;
        bearingToOtherDegrees = bearingToOtherDegrees * 180 / PI;

        // Distance to Waypoint
        float d = GeoCoord::latLongToMeter(DegD(wp.latitude_i), DegD(wp.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            if (d < (2 * MILES_TO_FEET))
                snprintf(distStr, sizeof(distStr), "%.0fft   %.0f°", d * METERS_TO_FEET, bearingToOtherDegrees);
            else
                snprintf(distStr, sizeof(distStr), "%.1fmi   %.0f°", d * METERS_TO_FEET / MILES_TO_FEET, bearingToOtherDegrees);
        } else {
            if (d < 2000)
                snprintf(distStr, sizeof(distStr), "%.0fm   %.0f°", d, bearingToOtherDegrees);
            else
                snprintf(distStr, sizeof(distStr), "%.1fkm   %.0f°", d / 1000, bearingToOtherDegrees);
        }

    }

    // If our node doesn't have position
    else {
        // ? in the compass
        display->drawString(compassX - FONT_HEIGHT_SMALL / 4, compassY - FONT_HEIGHT_SMALL / 2, "?");

        // ? in the distance field
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            strncpy(distStr, "? mi ?°", sizeof(distStr));
        else
            strncpy(distStr, "? km ?°", sizeof(distStr));
    }

    // Draw compass circle
    display->drawCircle(compassX, compassY, compassDiam / 2);

    // Undo color-inversion, if set prior to drawing header
    // Unsure of expected behavior? For now: copy drawNodeInfo
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }

    // Must be after distStr is populated
    screen->drawColumns(display, x, y, fields);
}
#endif
