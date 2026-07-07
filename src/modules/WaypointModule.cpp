#include "WaypointModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/CompassRenderer.h"
#include "meshUtils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#if !MESHTASTIC_EXCLUDE_WAYPOINT
#include "ExternalNotificationModule.h"
#include "GeofenceModule.h"
#include "WaypointStore.h"
#endif

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_WAYPOINT
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/TimeFormatters.h"
#include "graphics/draw/NodeListRenderer.h"
#include "graphics/draw/UIRenderer.h"
#include "main.h"
#endif

WaypointModule *waypointModule;

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_WAYPOINT
namespace
{

constexpr int16_t WAYPOINT_ROW_GAP = 2;

std::string utf8FromCodepoint(uint32_t codepoint)
{
    char buf[5] = {};
    if (codepoint <= 0x7F) {
        buf[0] = static_cast<char>(codepoint);
        return std::string(buf, 1);
    }
    if (codepoint <= 0x7FF) {
        buf[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        buf[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 2);
    }
    if (codepoint <= 0xFFFF) {
        buf[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 3);
    }
    if (codepoint <= 0x10FFFF) {
        buf[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 4);
    }
    return "";
}

void drawFallbackWaypointIcon(OLEDDisplay *display, int16_t left, int16_t top, uint16_t boxSize)
{
    const int16_t cx = left + (boxSize / 2);
    const int16_t circleY = top + std::max<int16_t>(2, boxSize / 3);
    const int16_t r = std::max<int16_t>(1, boxSize / 4);
    display->drawCircle(cx, circleY, r);
    display->drawLine(cx, circleY + r, cx, top + boxSize - 2);
    display->setPixel(cx - 1, top + boxSize - 2);
    display->setPixel(cx + 1, top + boxSize - 2);
}

void drawWaypointIcon(OLEDDisplay *display, const meshtastic_Waypoint &wp, int16_t left, int16_t top, uint16_t boxSize)
{
    if (!wp.icon) {
        drawFallbackWaypointIcon(display, left, top, boxSize);
        return;
    }

    const std::string utf8 = utf8FromCodepoint(wp.icon);
    if (utf8.empty()) {
        drawFallbackWaypointIcon(display, left, top, boxSize);
        return;
    }

    graphics::UIRenderer::drawStringWithEmotes(display, left, top, utf8, FONT_HEIGHT_SMALL, 1, false);
}

void formatWaypointDistance(char *out, size_t outSize, float meters, bool includeBearing, float bearingDegrees)
{
    out[0] = '\0';

    if (includeBearing) {
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            const float feet = meters * METERS_TO_FEET;
            snprintf(out, outSize, feet < (2 * MILES_TO_FEET) ? "%.0fft %.0f deg" : "%.1fmi %.0f deg",
                     feet < (2 * MILES_TO_FEET) ? feet : feet / MILES_TO_FEET, bearingDegrees);
        } else {
            snprintf(out, outSize, meters < 2000 ? "%.0fm %.0f deg" : "%.1fkm %.0f deg", meters < 2000 ? meters : meters / 1000,
                     bearingDegrees);
        }
        return;
    }

    if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
        const float feet = meters * METERS_TO_FEET;
        snprintf(out, outSize, feet < (2 * MILES_TO_FEET) ? "%.0fft" : "%.1fmi",
                 feet < (2 * MILES_TO_FEET) ? feet : feet / MILES_TO_FEET);
    } else {
        snprintf(out, outSize, meters < 2000 ? "%.0fm" : "%.1fkm", meters < 2000 ? meters : meters / 1000);
    }
}

void formatWaypointCoordinates(char *out, size_t outSize, const meshtastic_Waypoint &wp)
{
    if (!(wp.has_latitude_i && wp.has_longitude_i)) {
        snprintf(out, outSize, "--");
        return;
    }

    snprintf(out, outSize, "%.4f,%.4f", wp.latitude_i * 1e-7, wp.longitude_i * 1e-7);
}

void formatWaypointExpire(char *out, size_t outSize, const meshtastic_Waypoint &wp)
{
    if (wp.expire == 0) {
        out[0] = '\0';
        return;
    }

    const uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
    if (now == 0) {
        out[0] = '\0';
        return;
    }
    if (wp.expire <= now) {
        snprintf(out, outSize, "0m");
        return;
    }

    const uint32_t left = wp.expire - now;
    if (left < 3600)
        snprintf(out, outSize, "%lum", (unsigned long)((left + 59) / 60));
    else if (left < 86400)
        snprintf(out, outSize, "%luh", (unsigned long)((left + 3599) / 3600));
    else
        snprintf(out, outSize, "%lud", (unsigned long)((left + 86399) / 86400));
}

std::string trimmedWaypointText(const char *text)
{
    if (!text)
        return "";

    std::string value(text);
    const auto first = std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); });
    if (first == value.end())
        return "";

    const auto last = std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base();
    return std::string(first, last);
}

size_t collectDrawableWaypoints(const StoredWaypoint *entries[], size_t maxEntries)
{
    size_t count = 0;
    for (const StoredWaypoint &entry : waypointStore.getWaypoints()) {
        if (WaypointStore::isExpired(entry))
            continue;
        if (count < maxEntries)
            entries[count] = &entry;
        ++count;
    }

    return count;
}

void drawDottedHorizontalDivider(OLEDDisplay *display, int16_t xStart, int16_t xEnd, int16_t y)
{
    for (int16_t x = xStart; x <= xEnd; x += 2) {
        display->setPixel(x, y);
    }
}

void notifyWaypointReceived(const StoredWaypoint &stored)
{
    if (screen) {
        const std::string waypointName = trimmedWaypointText(stored.waypoint.name);
        if (!waypointName.empty()) {
            char banner[96];
            snprintf(banner, sizeof(banner), "New Waypoint\n%s", waypointName.c_str());
            screen->showSimpleBanner(banner, 3000);
        } else {
            screen->showSimpleBanner("New Waypoint", 3000);
        }
    }

    if (externalNotificationModule)
        externalNotificationModule->startNotification();
}

} // namespace
#endif

ProcessMessage WaypointModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Received waypoint msg from=0x%08x, id=0x%08x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif
#if MESHTASTIC_EXCLUDE_WAYPOINT
    (void)mp;
    return ProcessMessage::CONTINUE;
#else
    StoredWaypoint stored;
    if (!waypointStore.addFromPacket(mp, &stored))
        return ProcessMessage::CONTINUE;

    if (geofenceModule)
        geofenceModule->onWaypointReceived(stored.waypoint, stored.creatorNodeNum);

    powerFSM.trigger(EVENT_RECEIVED_MSG);

#if HAS_SCREEN
    if (!isFromUs(&mp) && !WaypointStore::isExpired(stored))
        notifyWaypointReceived(stored);

    UIFrameEvent e;
    // Refresh the waypoint frame list quietly; new waypoints alert via banner/sound but do not
    // steal focus from the screen the user is already on.
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND;

    notifyObservers(&e);

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
#endif
}

#if HAS_SCREEN
bool WaypointModule::shouldDraw()
{
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    if (!screen || waypointStore.getWaypoints().empty())
        return false;

    const StoredWaypoint *entries[WAYPOINT_HISTORY_LIMIT];
    return collectDrawableWaypoints(entries, WAYPOINT_HISTORY_LIMIT) > 0;
#else
    return false;
#endif
}

void WaypointModule::onDeviceTimeChanged()
{
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    if (!screen)
        return;

    UIFrameEvent e;
    e.action = shouldDraw() ? UIFrameEvent::Action::REGENERATE_FRAMESET : UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND;
    notifyObservers(&e);
#endif
}

/// Draw the newest non-expired waypoints we received
void WaypointModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
#if MESHTASTIC_EXCLUDE_WAYPOINT
    (void)display;
    (void)x;
    (void)y;
    return;
#else
    if (!screen)
        return;

    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    const StoredWaypoint *entries[WAYPOINT_HISTORY_LIMIT];
    const size_t totalWaypoints = collectDrawableWaypoints(entries, WAYPOINT_HISTORY_LIMIT);
    if (totalWaypoints == 0)
        return;

    const char *titleStr = (totalWaypoints == 1) ? "Waypoint" : "Waypoints";
    graphics::drawCommonHeader(display, x, y, titleStr);
    const int *textPos = graphics::getTextPositions(display);

    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const bool hasOwnPositionFix = (ourNode && nodeDB->hasValidPosition(ourNode));
    meshtastic_PositionLite ownPos = meshtastic_PositionLite_init_zero;
    const bool haveOwnPos = ourNode && nodeDB->copyNodePosition(ourNode->num, ownPos);

    const uint16_t iconWidth = FONT_HEIGHT_SMALL;
    const uint16_t iconGap = 3;
    const uint16_t nameX = iconWidth + iconGap;
    const int16_t contentBottom = display->getHeight() - 1;
    int16_t rowTop = textPos[1];

    for (size_t i = 0; i < totalWaypoints; ++i) {
        const StoredWaypoint &entry = *entries[i];
        const meshtastic_Waypoint &wp = entry.waypoint;

        char safeName[sizeof(wp.name)];
        memcpy(safeName, wp.name, sizeof(safeName));
        safeName[sizeof(safeName) - 1] = '\0';
        sanitizeUtf8(safeName, sizeof(safeName));

        char safeDescription[sizeof(wp.description)];
        memcpy(safeDescription, wp.description, sizeof(safeDescription));
        safeDescription[sizeof(safeDescription) - 1] = '\0';
        sanitizeUtf8(safeDescription, sizeof(safeDescription));

        char distStr[20] = "";
        char coordStr[40];
        char expireStr[16];
        formatWaypointCoordinates(coordStr, sizeof(coordStr), wp);
        formatWaypointExpire(expireStr, sizeof(expireStr), wp);

        const std::string description = trimmedWaypointText(safeDescription);
        const bool hasDescription = !description.empty();
        const int16_t row1Y = rowTop;
        const int16_t row2Y = row1Y + FONT_HEIGHT_SMALL + 1;
        const int16_t rowMetaY = hasDescription ? (row2Y + FONT_HEIGHT_SMALL + 1) : row2Y;
        const int16_t cardBottom = rowMetaY + FONT_HEIGHT_SMALL;
        if (cardBottom > contentBottom)
            break;

        bool showCompass = false;
        float myHeading = 0.0f;
        float bearingToOther = 0.0f;
        if (hasOwnPositionFix && haveOwnPos && wp.has_latitude_i && wp.has_longitude_i) {
            const float d = GeoCoord::latLongToMeter(DegD(wp.latitude_i), DegD(wp.longitude_i), DegD(ownPos.latitude_i),
                                                     DegD(ownPos.longitude_i));
            formatWaypointDistance(distStr, sizeof(distStr), d, false, 0.0f);

            if (graphics::CompassRenderer::getHeadingRadians(DegD(ownPos.latitude_i), DegD(ownPos.longitude_i), myHeading)) {
                showCompass = true;
                bearingToOther = GeoCoord::bearing(DegD(ownPos.latitude_i), DegD(ownPos.longitude_i), DegD(wp.latitude_i),
                                                   DegD(wp.longitude_i));
                bearingToOther = graphics::CompassRenderer::adjustBearingForCompassMode(bearingToOther, myHeading);
            }
        }

        const int16_t compactArrowCenterX = display->getWidth() - ((FONT_HEIGHT_SMALL > 10) ? 9 : 7);
        const int16_t compactArrowCenterY = (hasDescription ? row2Y : row1Y) + (FONT_HEIGHT_SMALL / 2);
        const int16_t compactContentRight = compactArrowCenterX - 8;
        const char *distanceLabel = distStr[0] ? distStr : "--";
        const char *expireLabel = expireStr[0] ? expireStr : "--";
        const uint16_t metaWidth =
            std::max<uint16_t>(display->getStringWidth(distanceLabel), display->getStringWidth(expireLabel)) + 4;
        const int16_t metaLeft = std::max<int16_t>(nameX + 16, compactContentRight - metaWidth);
        const int16_t textRight = metaLeft - 4;
        const uint16_t nameWidth = (textRight > nameX) ? (textRight - nameX) : 0;
        const std::string shownName = graphics::UIRenderer::truncateStringWithEmotes(display, safeName, nameWidth);
        const std::string shownDescription =
            hasDescription ? graphics::UIRenderer::truncateStringWithEmotes(display, description, nameWidth) : std::string();

        drawWaypointIcon(display, wp, 0, row1Y, iconWidth);
        graphics::UIRenderer::drawStringWithEmotes(display, nameX, row1Y, shownName, FONT_HEIGHT_SMALL, 1, false);
        const int16_t underlineY = row1Y + FONT_HEIGHT_SMALL;
        const int16_t underlineRight =
            std::min<int16_t>(textRight, nameX + graphics::UIRenderer::measureStringWithEmotes(display, shownName) - 1);
        if (underlineRight >= nameX)
            display->drawLine(nameX, underlineY, underlineRight, underlineY);

        if (hasDescription)
            graphics::UIRenderer::drawStringWithEmotes(display, nameX, row2Y, shownDescription, FONT_HEIGHT_SMALL, 1, false);

        if (showCompass)
            graphics::NodeListRenderer::drawRelativeCompassArrow(display, compactArrowCenterX, compactArrowCenterY,
                                                                 graphics::CompassRenderer::radiansToDegrees360(bearingToOther));

        display->drawStringMaxWidth(nameX, rowMetaY, nameWidth, coordStr);
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(metaLeft + metaWidth - 1, row1Y, distanceLabel);
        display->drawString(metaLeft + metaWidth - 1, rowMetaY, expireLabel);
        display->setTextAlignment(TEXT_ALIGN_LEFT);

        const int16_t separatorY = cardBottom + 1;
        const int16_t nextRowTop = separatorY + WAYPOINT_ROW_GAP;
        if (i + 1 < totalWaypoints && nextRowTop + ((FONT_HEIGHT_SMALL * 2) + 1) <= contentBottom) {
            drawDottedHorizontalDivider(display, 0, display->getWidth() - 1, separatorY);
            rowTop = nextRowTop;
        } else {
            break;
        }
    }
#endif
}
#endif
