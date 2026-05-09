#include "WaypointModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/CompassRenderer.h"

#if HAS_SCREEN
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/TimeFormatters.h"
#include "graphics/draw/NodeListRenderer.h"
#include "main.h"
#endif

WaypointModule *waypointModule;

ProcessMessage WaypointModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
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
    if (!screen || !devicestate.has_rx_waypoint)
        return false;

    meshtastic_Waypoint wp{}; // <- replaces memset
    if (pb_decode_from_bytes(devicestate.rx_waypoint.decoded.payload.bytes, devicestate.rx_waypoint.decoded.payload.size,
                             &meshtastic_Waypoint_msg, &wp)) {
        return wp.expire > getTime();
    }
    return false; // no LOG_ERROR, no flag writes
#else
    return false;
#endif
}

/// Draw the last waypoint we received
void WaypointModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!screen)
        return;
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    // === Set Title
    const char *titleStr = "Waypoint";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);
    const int *textPos = graphics::getTextPositions(display);

    // Decode the waypoint
    const meshtastic_MeshPacket &mp = devicestate.rx_waypoint;
    meshtastic_Waypoint wp{};
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Waypoint_msg, &wp)) {
        devicestate.has_rx_waypoint = false;
        return;
    }

    // Get timestamp info. Will pass as a field to drawColumns
    char lastStr[20];
    getTimeAgoStr(sinceReceived(&mp), lastStr, sizeof(lastStr));

    // Will contain distance information, passed as a field to drawColumns
    char distStr[20] = "";

    // Get our node, to use our own position
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());

    // Match compass sizing/placement to favorite node screen logic.
    const int w = display->getWidth();
    int16_t compassRadius = 8;
    int16_t compassX = x + w - compassRadius - 8;
    int16_t compassY = y + display->getHeight() / 2;

    if (SCREEN_WIDTH > SCREEN_HEIGHT) {
        const int16_t topY = textPos[1];
        const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1);
        const int16_t usableHeight = bottomY - topY - 5;
        compassRadius = usableHeight / 2;
        if (compassRadius < 8)
            compassRadius = 8;
        compassX = x + SCREEN_WIDTH - compassRadius - 8;
        compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;
    } else {
        // Waypoint content uses rows 1..4, so place the compass below that block.
        const int yBelowContent = textPos[4] + FONT_HEIGHT_SMALL + 2;
        const int margin = 4;
#if defined(USE_EINK)
        const int iconSize = (graphics::currentResolution == graphics::ScreenResolution::High) ? 16 : 8;
        const int navBarHeight = iconSize + 6;
#else
        const int navBarHeight = 0;
#endif
        const int availableHeight = SCREEN_HEIGHT - yBelowContent - navBarHeight - margin;
        if (availableHeight > 0) {
            compassRadius = availableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            if (compassRadius * 2 > SCREEN_WIDTH - 16)
                compassRadius = (SCREEN_WIDTH - 16) / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            compassX = x + SCREEN_WIDTH / 2;
            compassY = yBelowContent + availableHeight / 2;
        }
    }
    const uint16_t compassDiam = compassRadius * 2;

    const bool hasOwnPositionFix = (ourNode && nodeDB->hasValidPosition(ourNode));
    const char *statusLine1 = nullptr;
    const char *statusLine2 = nullptr;

    // Distance only needs our own position fix; compass/bearing additionally needs heading.
    if (hasOwnPositionFix) {
        const meshtastic_PositionLite &op = ourNode->position;
        const float d =
            GeoCoord::latLongToMeter(DegD(wp.latitude_i), DegD(wp.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));

        // Always show distance once we have an own-position fix, even without heading.
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            float feet = d * METERS_TO_FEET;
            snprintf(distStr, sizeof(distStr), feet < (2 * MILES_TO_FEET) ? "%.0fft" : "%.1fmi",
                     feet < (2 * MILES_TO_FEET) ? feet : feet / MILES_TO_FEET);
        } else {
            snprintf(distStr, sizeof(distStr), d < 2000 ? "%.0fm" : "%.1fkm", d < 2000 ? d : d / 1000);
        }

        float myHeading = 0.0f;
        const bool hasHeading =
            graphics::CompassRenderer::getHeadingRadians(DegD(op.latitude_i), DegD(op.longitude_i), myHeading);
        if (hasHeading) {
            // Draw compass circle
            display->drawCircle(compassX, compassY, compassRadius);
            graphics::CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading, compassRadius);

            // Compass bearing to waypoint
            float bearingToOther =
                GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(wp.latitude_i), DegD(wp.longitude_i));
            bearingToOther = graphics::CompassRenderer::adjustBearingForCompassMode(bearingToOther, myHeading);
            graphics::CompassRenderer::drawNodeHeading(display, compassX, compassY, compassDiam, bearingToOther);

            const float bearingToOtherDegrees = graphics::CompassRenderer::radiansToDegrees360(bearingToOther);

            // Distance to waypoint with relative bearing when heading is available.
            if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
                float feet = d * METERS_TO_FEET;
                snprintf(distStr, sizeof(distStr), feet < (2 * MILES_TO_FEET) ? "%.0fft   %.0f°" : "%.1fmi   %.0f°",
                         feet < (2 * MILES_TO_FEET) ? feet : feet / MILES_TO_FEET, bearingToOtherDegrees);
            } else {
                snprintf(distStr, sizeof(distStr), d < 2000 ? "%.0fm   %.0f°" : "%.1fkm   %.0f°", d < 2000 ? d : d / 1000,
                         bearingToOtherDegrees);
            }

        } else {
            statusLine1 = "No";
            statusLine2 = "Heading";
        }
    } else {
        // No own fix yet, so compass/bearing data would be misleading.
        statusLine1 = "No";
        statusLine2 = "Fix";
    }

    if (statusLine1) {
        display->drawCircle(compassX, compassY, compassRadius);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(compassX, compassY - FONT_HEIGHT_SMALL, statusLine1);
        display->drawString(compassX, compassY, statusLine2);
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Something above me changes to a different alignment, forcing a fix here!
    display->drawString(0, textPos[line++], lastStr);
    display->drawString(0, textPos[line++], wp.name);
    display->drawString(0, textPos[line++], wp.description);
    if (distStr[0])
        display->drawString(0, textPos[line++], distStr);
}
#endif
