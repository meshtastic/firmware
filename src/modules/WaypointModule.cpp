#include "WaypointModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/CompassRenderer.h"
#include <algorithm>
#include <cctype>
#include <string>
#include "meshUtils.h"

#if !MESHTASTIC_EXCLUDE_WAYPOINT
#include "GeofenceModule.h"
#include "WaypointStore.h"
#endif

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_WAYPOINT
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/TimeFormatters.h"
#include "graphics/draw/NodeListRenderer.h"
#include "main.h"
#endif

WaypointModule *waypointModule;

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_WAYPOINT
namespace
{

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
    display->drawPixel(cx - 1, top + boxSize - 2);
    display->drawPixel(cx + 1, top + boxSize - 2);
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

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(left, top, utf8.c_str());
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
        snprintf(out, outSize, "exp");
        return;
    }

    const uint32_t left = wp.expire - now;
    if (left < 60)
        snprintf(out, outSize, "exp %lus", (unsigned long)left);
    else if (left < 3600)
        snprintf(out, outSize, "exp %lum", (unsigned long)((left + 59) / 60));
    else if (left < 86400)
        snprintf(out, outSize, "exp %luh", (unsigned long)((left + 3599) / 3600));
    else
        snprintf(out, outSize, "exp %lud", (unsigned long)((left + 86399) / 86400));
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

} // namespace

const StoredWaypoint *WaypointModule::latestDrawableWaypoint()
{
    for (const StoredWaypoint &entry : waypointStore.getWaypoints()) {
        if (!WaypointStore::isExpired(entry))
            return &entry;
    }

    return nullptr;
}
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
        geofenceModule->onWaypointReceived(stored.waypoint);

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
#endif
}

#if HAS_SCREEN
bool WaypointModule::shouldDraw()
{
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    if (!screen || waypointStore.getWaypoints().empty())
        return false;

    return latestDrawableWaypoint() != nullptr;
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

/// Draw the newest non-expired waypoint we received
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
    int line = 1;

    const char *titleStr = "Waypoint";
    graphics::drawCommonHeader(display, x, y, titleStr);
    const int *textPos = graphics::getTextPositions(display);

    const StoredWaypoint *entry = latestDrawableWaypoint();
    if (!entry)
        return;
    const meshtastic_Waypoint &wp = entry->waypoint;

    // Sanitize before these reach the OLED renderer (defense-in-depth vs PB_VALIDATE_UTF8).
    sanitizeUtf8(wp.name, sizeof(wp.name));
    sanitizeUtf8(wp.description, sizeof(wp.description));

    // Get timestamp info. Will pass as a field to drawColumns
    char lastStr[20];
    getTimeAgoStr(WaypointStore::age(*entry), lastStr, sizeof(lastStr));

    char distStr[20] = "";
    char coordStr[40];
    char expireStr[16];
    formatWaypointCoordinates(coordStr, sizeof(coordStr), wp);
    formatWaypointExpire(expireStr, sizeof(expireStr), wp);

    const std::string description = trimmedWaypointText(wp.description);
    const bool hasDescription = !description.empty();
    const int topTextLines = hasDescription ? 4 : 3;

    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
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
        const int yBelowContent = textPos[topTextLines] + FONT_HEIGHT_SMALL + 2;
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

    meshtastic_PositionLite ownPos;
    const bool haveOwnPos = ourNode && nodeDB->copyNodePosition(ourNode->num, ownPos);
    if (hasOwnPositionFix && haveOwnPos) {
        const float d = GeoCoord::latLongToMeter(DegD(wp.latitude_i), DegD(wp.longitude_i), DegD(ownPos.latitude_i),
                                                 DegD(ownPos.longitude_i));

        float myHeading = 0.0f;
        const bool hasHeading =
            graphics::CompassRenderer::getHeadingRadians(DegD(ownPos.latitude_i), DegD(ownPos.longitude_i), myHeading);
        if (hasHeading) {
            display->drawCircle(compassX, compassY, compassRadius);
            graphics::CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading, compassRadius);

            float bearingToOther =
                GeoCoord::bearing(DegD(ownPos.latitude_i), DegD(ownPos.longitude_i), DegD(wp.latitude_i), DegD(wp.longitude_i));
            bearingToOther = graphics::CompassRenderer::adjustBearingForCompassMode(bearingToOther, myHeading);
            graphics::CompassRenderer::drawNodeHeading(display, compassX, compassY, compassDiam, bearingToOther);

            const float bearingToOtherDegrees = graphics::CompassRenderer::radiansToDegrees360(bearingToOther);
            formatWaypointDistance(distStr, sizeof(distStr), d, true, bearingToOtherDegrees);
        } else {
            formatWaypointDistance(distStr, sizeof(distStr), d, false, 0.0f);
            statusLine1 = "No";
            statusLine2 = "Heading";
        }
    } else {
        statusLine1 = "No";
        statusLine2 = "Fix";
    }

    if (statusLine1) {
        display->drawCircle(compassX, compassY, compassRadius);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(compassX, compassY - FONT_HEIGHT_SMALL, statusLine1);
        display->drawString(compassX, compassY, statusLine2);
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const uint16_t iconWidth = FONT_HEIGHT_SMALL;
    const uint16_t iconGap = 3;
    const uint16_t nameX = iconWidth + iconGap;

    const int16_t row1Y = textPos[line++];
    const uint16_t distWidth = distStr[0] ? display->getStringWidth(distStr) + 4 : 0;
    const uint16_t nameWidth =
        (display->getWidth() > nameX + distWidth) ? (display->getWidth() - nameX - distWidth) : (display->getWidth() - nameX);
    drawWaypointIcon(display, wp, 0, row1Y, iconWidth);
    display->drawStringMaxWidth(nameX, row1Y, nameWidth, wp.name);
    if (distStr[0]) {
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(display->getWidth(), row1Y, distStr);
        display->setTextAlignment(TEXT_ALIGN_LEFT);
    }

    if (hasDescription)
        display->drawStringMaxWidth(nameX, textPos[line++], display->getWidth() - nameX, description.c_str());

    const int16_t rowMetaY = textPos[line++];
    const uint16_t expireWidth = expireStr[0] ? display->getStringWidth(expireStr) + 4 : 0;
    const uint16_t coordWidth = (display->getWidth() > expireWidth) ? (display->getWidth() - expireWidth) : display->getWidth();
    display->drawStringMaxWidth(0, rowMetaY, coordWidth, coordStr);
    if (expireStr[0]) {
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(display->getWidth(), rowMetaY, expireStr);
        display->setTextAlignment(TEXT_ALIGN_LEFT);
    }

    if (line <= 4)
        display->drawStringMaxWidth(0, textPos[line++], display->getWidth(), lastStr);
#endif
}
#endif
