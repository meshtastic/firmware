#include "configuration.h"
#if HAS_SCREEN
#include "CompassRenderer.h"
#include "GPSStatus.h"
#include "NodeDB.h"
#include "NodeListRenderer.h"
#include "UIRenderer.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "target_specific.h"
#include <OLEDDisplay.h>
#include <RTC.h>
#include <cstring>

#if !MESHTASTIC_EXCLUDE_GPS

// External variables
extern graphics::Screen *screen;

namespace graphics
{

// GeoCoord object for coordinate conversions
extern GeoCoord geoCoord;

// Threshold values for the GPS lock accuracy bar display
extern uint32_t dopThresholds[5];

namespace UIRenderer
{

// Draw GPS status summary
void drawGps(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    if (config.position.fixed_position) {
        // GPS coordinates are currently fixed
        display->drawString(x - 1, y - 2, "Fixed GPS");
        if (config.display.heading_bold)
            display->drawString(x, y - 2, "Fixed GPS");
        return;
    }
    if (!gps->getIsConnected()) {
        display->drawString(x, y - 2, "No GPS");
        if (config.display.heading_bold)
            display->drawString(x + 1, y - 2, "No GPS");
        return;
    }
    // Adjust position if we're going to draw too wide
    int maxDrawWidth = 6; // Position icon

    if (!gps->getHasLock()) {
        maxDrawWidth += display->getStringWidth("No sats") + 2; // icon + text + buffer
    } else {
        maxDrawWidth += (5 * 2) + 8 + display->getStringWidth("99") + 2; // bars + sat icon + text + buffer
    }

    if (x + maxDrawWidth > display->getWidth()) {
        x = display->getWidth() - maxDrawWidth;
        if (x < 0)
            x = 0; // Clamp to screen
    }

    display->drawFastImage(x, y, 6, 8, gps->getHasLock() ? imgPositionSolid : imgPositionEmpty);
    if (!gps->getHasLock()) {
        // Draw "No sats" to the right of the icon with slightly more gap
        int textX = x + 9; // 6 (icon) + 3px spacing
        display->drawString(textX, y - 3, "No sats");
        if (config.display.heading_bold)
            display->drawString(textX + 1, y - 3, "No sats");
        return;
    } else {
        char satsString[3];
        uint8_t bar[2] = {0};

        // Draw DOP signal bars
        for (int i = 0; i < 5; i++) {
            if (gps->getDOP() <= dopThresholds[i])
                bar[0] = ~((1 << (5 - i)) - 1);
            else
                bar[0] = 0b10000000;

            display->drawFastImage(x + 9 + (i * 2), y, 2, 8, bar);
        }

        // Draw satellite image
        display->drawFastImage(x + 24, y, 8, 8, imgSatellite);

        // Draw the number of satellites
        snprintf(satsString, sizeof(satsString), "%u", gps->getNumSatellites());
        int textX = x + 34;
        display->drawString(textX, y - 2, satsString);
        if (config.display.heading_bold)
            display->drawString(textX + 1, y - 2, satsString);
    }
}

// Draw status when GPS is disabled or not present
void drawGpsPowerStatus(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    const char *displayLine;
    int pos;
    if (y < FONT_HEIGHT_SMALL) { // Line 1: use short string
        displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        pos = display->getWidth() - display->getStringWidth(displayLine);
    } else {
        displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "GPS not present"
                                                                                                       : "GPS is disabled";
        pos = (display->getWidth() - display->getStringWidth(displayLine)) / 2;
    }
    display->drawString(x + pos, y, displayLine);
}

void drawGpsAltitude(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    char displayLine[32];
    if (!gps->getIsConnected() && !config.position.fixed_position) {
        // displayLine = "No GPS Module";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        // displayLine = "No GPS Lock";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {
        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            snprintf(displayLine, sizeof(displayLine), "Altitude: %.0fft", geoCoord.getAltitude() * METERS_TO_FEET);
        else
            snprintf(displayLine, sizeof(displayLine), "Altitude: %.0im", geoCoord.getAltitude());
        display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    }
}

// Draw GPS status coordinates
void drawGpsCoordinates(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    auto gpsFormat = config.display.gps_format;
    char displayLine[32];

    if (!gps->getIsConnected() && !config.position.fixed_position) {
        strcpy(displayLine, "No GPS present");
        display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        strcpy(displayLine, "No GPS Lock");
        display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {

        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));

        if (gpsFormat != meshtastic_Config_DisplayConfig_GpsCoordinateFormat_DMS) {
            char coordinateLine[22];
            if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_DEC) { // Decimal Degrees
                snprintf(coordinateLine, sizeof(coordinateLine), "%f %f", geoCoord.getLatitude() * 1e-7,
                         geoCoord.getLongitude() * 1e-7);
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_UTM) { // Universal Transverse Mercator
                snprintf(coordinateLine, sizeof(coordinateLine), "%2i%1c %06u %07u", geoCoord.getUTMZone(), geoCoord.getUTMBand(),
                         geoCoord.getUTMEasting(), geoCoord.getUTMNorthing());
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_MGRS) { // Military Grid Reference System
                snprintf(coordinateLine, sizeof(coordinateLine), "%2i%1c %1c%1c %05u %05u", geoCoord.getMGRSZone(),
                         geoCoord.getMGRSBand(), geoCoord.getMGRSEast100k(), geoCoord.getMGRSNorth100k(),
                         geoCoord.getMGRSEasting(), geoCoord.getMGRSNorthing());
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_OLC) { // Open Location Code
                geoCoord.getOLCCode(coordinateLine);
            } else if (gpsFormat == meshtastic_Config_DisplayConfig_GpsCoordinateFormat_OSGR) { // Ordnance Survey Grid Reference
                if (geoCoord.getOSGRE100k() == 'I' || geoCoord.getOSGRN100k() == 'I') // OSGR is only valid around the UK region
                    snprintf(coordinateLine, sizeof(coordinateLine), "%s", "Out of Boundary");
                else
                    snprintf(coordinateLine, sizeof(coordinateLine), "%1c%1c %05u %05u", geoCoord.getOSGRE100k(),
                             geoCoord.getOSGRN100k(), geoCoord.getOSGREasting(), geoCoord.getOSGRNorthing());
            }

            // If fixed position, display text "Fixed GPS" alternating with the coordinates.
            if (config.position.fixed_position) {
                if ((millis() / 10000) % 2) {
                    display->drawString(x + (display->getWidth() - (display->getStringWidth(coordinateLine))) / 2, y,
                                        coordinateLine);
                } else {
                    display->drawString(x + (display->getWidth() - (display->getStringWidth("Fixed GPS"))) / 2, y, "Fixed GPS");
                }
            } else {
                display->drawString(x + (display->getWidth() - (display->getStringWidth(coordinateLine))) / 2, y, coordinateLine);
            }
        } else {
            char latLine[22];
            char lonLine[22];
            snprintf(latLine, sizeof(latLine), "%2i° %2i' %2u\" %1c", geoCoord.getDMSLatDeg(), geoCoord.getDMSLatMin(),
                     geoCoord.getDMSLatSec(), geoCoord.getDMSLatCP());
            snprintf(lonLine, sizeof(lonLine), "%3i° %2i' %2u\" %1c", geoCoord.getDMSLonDeg(), geoCoord.getDMSLonMin(),
                     geoCoord.getDMSLonSec(), geoCoord.getDMSLonCP());
            display->drawString(x + (display->getWidth() - (display->getStringWidth(latLine))) / 2, y - FONT_HEIGHT_SMALL * 1,
                                latLine);
            display->drawString(x + (display->getWidth() - (display->getStringWidth(lonLine))) / 2, y, lonLine);
        }
    }
}

void drawBattery(OLEDDisplay *display, int16_t x, int16_t y, uint8_t *imgBuffer, const meshtastic::PowerStatus *powerStatus)
{
    static const uint8_t powerBar[3] = {0x81, 0xBD, 0xBD};
    static const uint8_t lightning[8] = {0xA1, 0xA1, 0xA5, 0xAD, 0xB5, 0xA5, 0x85, 0x85};

    // Clear the bar area inside the battery image
    for (int i = 1; i < 14; i++) {
        imgBuffer[i] = 0x81;
    }

    // Fill with lightning or power bars
    if (powerStatus->getIsCharging()) {
        memcpy(imgBuffer + 3, lightning, 8);
    } else {
        for (int i = 0; i < 4; i++) {
            if (powerStatus->getBatteryChargePercent() >= 25 * i)
                memcpy(imgBuffer + 1 + (i * 3), powerBar, 3);
        }
    }

    // Slightly more conservative scaling based on screen width
    int scale = 1;

    if (SCREEN_WIDTH >= 200)
        scale = 2;
    if (SCREEN_WIDTH >= 300)
        scale = 2; // Do NOT go higher than 2

    // Draw scaled battery image (16 columns × 8 rows)
    for (int col = 0; col < 16; col++) {
        uint8_t colBits = imgBuffer[col];
        for (int row = 0; row < 8; row++) {
            if (colBits & (1 << row)) {
                display->fillRect(x + col * scale, y + row * scale, scale, scale);
            }
        }
    }
}

// Draw nodes status
void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::NodeStatus *nodeStatus, int node_offset,
               bool show_total, String additional_words)
{
    char usersString[20];
    int nodes_online = (nodeStatus->getNumOnline() > 0) ? nodeStatus->getNumOnline() + node_offset : 0;

    snprintf(usersString, sizeof(usersString), "%d", nodes_online);

    if (show_total) {
        int nodes_total = (nodeStatus->getNumTotal() > 0) ? nodeStatus->getNumTotal() + node_offset : 0;
        snprintf(usersString, sizeof(usersString), "%d/%d", nodes_online, nodes_total);
    }

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS)) &&                                  \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
    display->drawFastImage(x, y + 3, 8, 8, imgUser);
#else
    display->drawFastImage(x, y + 1, 8, 8, imgUser);
#endif
    display->drawString(x + 10, y - 2, usersString);
    int string_offset = (SCREEN_WIDTH > 128) ? 2 : 1;
    if (additional_words.length() > 0) {
        display->drawString(x + 10 + display->getStringWidth(usersString) + string_offset, y - 2, additional_words.c_str());
        if (config.display.heading_bold)
            display->drawString(x + 11 + display->getStringWidth(usersString) + string_offset, y - 2, additional_words.c_str());
    }
}

// **********************
// * Favorite Node Info *
// **********************
void drawNodeInfo(OLEDDisplay *display, const OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // --- Cache favorite nodes for the current frame only, to save computation ---
    static std::vector<meshtastic_NodeInfoLite *> favoritedNodes;
    static int prevFrame = -1;

    // --- Only rebuild favorites list if we're on a new frame ---
    if (state->currentFrame != prevFrame) {
        prevFrame = state->currentFrame;
        favoritedNodes.clear();
        size_t total = nodeDB->getNumMeshNodes();
        for (size_t i = 0; i < total; i++) {
            meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            // Skip nulls and ourself
            if (!n || n->num == nodeDB->getNodeNum())
                continue;
            if (n->is_favorite)
                favoritedNodes.push_back(n);
        }
        // Keep a stable, consistent display order
        std::sort(favoritedNodes.begin(), favoritedNodes.end(),
                  [](const meshtastic_NodeInfoLite *a, const meshtastic_NodeInfoLite *b) { return a->num < b->num; });
    }
    if (favoritedNodes.empty())
        return;

    // --- Only display if index is valid ---
    int nodeIndex = state->currentFrame - (screen->frameCount - favoritedNodes.size());
    if (nodeIndex < 0 || nodeIndex >= (int)favoritedNodes.size())
        return;

    meshtastic_NodeInfoLite *node = favoritedNodes[nodeIndex];
    if (!node || node->num == nodeDB->getNodeNum() || !node->is_favorite)
        return;

    display->clear();

    // === Draw battery/time/mail header (common across screens) ===
    graphics::drawCommonHeader(display, x, y);

    // === Draw the short node name centered at the top, with bold shadow if set ===
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const int centerX = x + SCREEN_WIDTH / 2;
    const char *shortName = (node->has_user && haveGlyphs(node->user.short_name)) ? node->user.short_name : "Node";
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED)
        display->setColor(BLACK);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    display->drawString(centerX, textY, shortName);
    if (config.display.heading_bold)
        display->drawString(centerX + 1, textY, shortName);

    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // ===== DYNAMIC ROW STACKING WITH YOUR MACROS =====
    // 1. Each potential info row has a macro-defined Y position (not regular increments!).
    // 2. Each row is only shown if it has valid data.
    // 3. Each row "moves up" if previous are empty, so there are never any blank rows.
    // 4. The first line is ALWAYS at your macro position; subsequent lines use the next available macro slot.

    // List of available macro Y positions in order, from top to bottom.
    const int yPositions[5] = {moreCompactFirstLine, moreCompactSecondLine, moreCompactThirdLine, moreCompactFourthLine,
                               moreCompactFifthLine};
    int line = 0; // which slot to use next

    // === 1. Long Name (always try to show first) ===
    const char *username = (node->has_user && node->user.long_name[0]) ? node->user.long_name : nullptr;
    if (username && line < 5) {
        // Print node's long name (e.g. "Backpack Node")
        display->drawString(x, yPositions[line++], username);
    }

    // === 2. Signal and Hops (combined on one line, if available) ===
    // If both are present: "Sig: 97%  [2hops]"
    // If only one: show only that one
    char signalHopsStr[32] = "";
    bool haveSignal = false;
    int percentSignal = clamp((int)((node->snr + 10) * 5), 0, 100);

    // Always use "Sig" for the label
    const char *signalLabel = " Sig";

    // --- Build the Signal/Hops line ---
    // If SNR looks reasonable, show signal
    if ((int)((node->snr + 10) * 5) >= 0 && node->snr > -100) {
        snprintf(signalHopsStr, sizeof(signalHopsStr), "%s: %d%%", signalLabel, percentSignal);
        haveSignal = true;
    }
    // If hops is valid (>0), show right after signal
    if (node->hops_away > 0) {
        size_t len = strlen(signalHopsStr);
        // Decide between "1 Hop" and "N Hops"
        if (haveSignal) {
            snprintf(signalHopsStr + len, sizeof(signalHopsStr) - len, " [%d %s]", node->hops_away,
                     (node->hops_away == 1 ? "Hop" : "Hops"));
        } else {
            snprintf(signalHopsStr, sizeof(signalHopsStr), "[%d %s]", node->hops_away, (node->hops_away == 1 ? "Hop" : "Hops"));
        }
    }
    if (signalHopsStr[0] && line < 5) {
        display->drawString(x, yPositions[line++], signalHopsStr);
    }

    // === 3. Heard (last seen, skip if node never seen) ===
    char seenStr[20] = "";
    uint32_t seconds = sinceLastSeen(node);
    if (seconds != 0 && seconds != UINT32_MAX) {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        // Format as "Heard: Xm ago", "Heard: Xh ago", or "Heard: Xd ago"
        snprintf(seenStr, sizeof(seenStr), (days > 365 ? " Heard: ?" : " Heard: %d%c ago"),
                 (days    ? days
                  : hours ? hours
                          : minutes),
                 (days    ? 'd'
                  : hours ? 'h'
                          : 'm'));
    }
    if (seenStr[0] && line < 5) {
        display->drawString(x, yPositions[line++], seenStr);
    }

    // === 4. Uptime (only show if metric is present) ===
    char uptimeStr[32] = "";
    if (node->has_device_metrics && node->device_metrics.has_uptime_seconds) {
        uint32_t uptime = node->device_metrics.uptime_seconds;
        uint32_t days = uptime / 86400;
        uint32_t hours = (uptime % 86400) / 3600;
        uint32_t mins = (uptime % 3600) / 60;
        // Show as "Up: 2d 3h", "Up: 5h 14m", or "Up: 37m"
        if (days)
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %ud %uh", days, hours);
        else if (hours)
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %uh %um", hours, mins);
        else
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %um", mins);
    }
    if (uptimeStr[0] && line < 5) {
        display->drawString(x, yPositions[line++], uptimeStr);
    }

    // === 5. Distance (only if both nodes have GPS position) ===
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    char distStr[24] = ""; // Make buffer big enough for any string
    bool haveDistance = false;

    if (nodeDB->hasValidPosition(ourNode) && nodeDB->hasValidPosition(node)) {
        double lat1 = ourNode->position.latitude_i * 1e-7;
        double lon1 = ourNode->position.longitude_i * 1e-7;
        double lat2 = node->position.latitude_i * 1e-7;
        double lon2 = node->position.longitude_i * 1e-7;
        double earthRadiusKm = 6371.0;
        double dLat = (lat2 - lat1) * DEG_TO_RAD;
        double dLon = (lon2 - lon1) * DEG_TO_RAD;
        double a =
            sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLon / 2) * sin(dLon / 2);
        double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        double distanceKm = earthRadiusKm * c;

        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            double miles = distanceKm * 0.621371;
            if (miles < 0.1) {
                int feet = (int)(miles * 5280);
                if (feet > 0 && feet < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dft", feet);
                    haveDistance = true;
                } else if (feet >= 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: ¼mi");
                    haveDistance = true;
                }
            } else {
                int roundedMiles = (int)(miles + 0.5);
                if (roundedMiles > 0 && roundedMiles < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dmi", roundedMiles);
                    haveDistance = true;
                }
            }
        } else {
            if (distanceKm < 1.0) {
                int meters = (int)(distanceKm * 1000);
                if (meters > 0 && meters < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dm", meters);
                    haveDistance = true;
                } else if (meters >= 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: 1km");
                    haveDistance = true;
                }
            } else {
                int km = (int)(distanceKm + 0.5);
                if (km > 0 && km < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dkm", km);
                    haveDistance = true;
                }
            }
        }
    }
    // Only display if we actually have a value!
    if (haveDistance && distStr[0] && line < 5) {
        display->drawString(x, yPositions[line++], distStr);
    }

    // --- Compass Rendering: landscape (wide) screens use the original side-aligned logic ---
    if (SCREEN_WIDTH > SCREEN_HEIGHT) {
        bool showCompass = false;
        if (ourNode && (nodeDB->hasValidPosition(ourNode) || screen->hasHeading()) && nodeDB->hasValidPosition(node)) {
            showCompass = true;
        }
        if (showCompass) {
            const int16_t topY = compactFirstLine;
            const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1);
            const int16_t usableHeight = bottomY - topY - 5;
            int16_t compassRadius = usableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            const int16_t compassDiam = compassRadius * 2;
            const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;
            const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

            const auto &op = ourNode->position;
            float myHeading = screen->hasHeading() ? screen->getHeading() * PI / 180
                                                   : screen->estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
            CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading);

            const auto &p = node->position;
            /* unused
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            */
            float bearing = GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            if (!config.display.compass_north_top)
                bearing -= myHeading;
            CompassRenderer::drawNodeHeading(display, compassX, compassY, compassDiam, bearing);

            display->drawCircle(compassX, compassY, compassRadius);
        }
        // else show nothing
    } else {
        // Portrait or square: put compass at the bottom and centered, scaled to fit available space
        bool showCompass = false;
        if (ourNode && (nodeDB->hasValidPosition(ourNode) || screen->hasHeading()) && nodeDB->hasValidPosition(node)) {
            showCompass = true;
        }
        if (showCompass) {
            int yBelowContent = (line > 0 && line <= 5) ? (yPositions[line - 1] + FONT_HEIGHT_SMALL + 2) : moreCompactFirstLine;
            const int margin = 4;
// --------- PATCH FOR EINK NAV BAR (ONLY CHANGE BELOW) -----------
#if defined(USE_EINK)
            const int iconSize = (SCREEN_WIDTH > 128) ? 16 : 8;
            const int navBarHeight = iconSize + 6;
#else
            const int navBarHeight = 0;
#endif
            int availableHeight = SCREEN_HEIGHT - yBelowContent - navBarHeight - margin;
            // --------- END PATCH FOR EINK NAV BAR -----------

            if (availableHeight < FONT_HEIGHT_SMALL * 2)
                return;

            int compassRadius = availableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            if (compassRadius * 2 > SCREEN_WIDTH - 16)
                compassRadius = (SCREEN_WIDTH - 16) / 2;

            int compassX = x + SCREEN_WIDTH / 2;
            int compassY = yBelowContent + availableHeight / 2;

            const auto &op = ourNode->position;
            float myHeading = screen->hasHeading() ? screen->getHeading() * PI / 180
                                                   : screen->estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
            graphics::CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading);

            const auto &p = node->position;
            /* unused
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            */
            float bearing = GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            if (!config.display.compass_north_top)
                bearing -= myHeading;
            graphics::CompassRenderer::drawNodeHeading(display, compassX, compassY, compassRadius * 2, bearing);

            display->drawCircle(compassX, compassY, compassRadius);
        }
        // else show nothing
    }
}

// ****************************
// * Device Focused Screen    *
// ****************************
void drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Content below header ===

    // Determine if we need to show 4 or 5 rows on the screen
    int rows = 4;
    if (!config.bluetooth.enabled) {
        rows = 5;
    }

    // === First Row: Region / Channel Utilization and Uptime ===
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    // Display Region and Channel Utilization
    drawNodes(display, x + 1,
              ((rows == 4) ? compactFirstLine : ((SCREEN_HEIGHT > 64) ? compactFirstLine : moreCompactFirstLine)) + 2, nodeStatus,
              -1, false, "online");

    uint32_t uptime = millis() / 1000;
    char uptimeStr[6];
    uint32_t minutes = uptime / 60, hours = minutes / 60, days = hours / 24;

    if (days > 365) {
        snprintf(uptimeStr, sizeof(uptimeStr), "?");
    } else {
        snprintf(uptimeStr, sizeof(uptimeStr), "%u%c",
                 days      ? days
                 : hours   ? hours
                 : minutes ? minutes
                           : (int)uptime,
                 days      ? 'd'
                 : hours   ? 'h'
                 : minutes ? 'm'
                           : 's');
    }

    char uptimeFullStr[16];
    snprintf(uptimeFullStr, sizeof(uptimeFullStr), "Uptime: %s", uptimeStr);
    display->drawString(SCREEN_WIDTH - display->getStringWidth(uptimeFullStr),
                        ((rows == 4) ? compactFirstLine : ((SCREEN_HEIGHT > 64) ? compactFirstLine : moreCompactFirstLine)),
                        uptimeFullStr);

    // === Second Row: Satellites and Voltage ===
    config.display.heading_bold = false;

#if HAS_GPS
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        const char *displayLine;
        if (config.position.fixed_position) {
            displayLine = "Fixed GPS";
        } else {
            displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        }
        display->drawString(
            0, ((rows == 4) ? compactSecondLine : ((SCREEN_HEIGHT > 64) ? compactSecondLine : moreCompactSecondLine)),
            displayLine);
    } else {
        UIRenderer::drawGps(
            display, 0,
            ((rows == 4) ? compactSecondLine : ((SCREEN_HEIGHT > 64) ? compactSecondLine : moreCompactSecondLine)) + 3,
            gpsStatus);
    }
#endif

    if (powerStatus->getHasBattery()) {
        char batStr[20];
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;
        snprintf(batStr, sizeof(batStr), "%01d.%02dV", batV, batCv);
        display->drawString(
            x + SCREEN_WIDTH - display->getStringWidth(batStr),
            ((rows == 4) ? compactSecondLine : ((SCREEN_HEIGHT > 64) ? compactSecondLine : moreCompactSecondLine)), batStr);
    } else {
        display->drawString(
            x + SCREEN_WIDTH - display->getStringWidth("USB"),
            ((rows == 4) ? compactSecondLine : ((SCREEN_HEIGHT > 64) ? compactSecondLine : moreCompactSecondLine)), "USB");
    }

    config.display.heading_bold = origBold;

    // === Third Row: Bluetooth Off (Only If Actually Off) ===
    if (!config.bluetooth.enabled) {
        display->drawString(
            0, ((rows == 4) ? compactThirdLine : ((SCREEN_HEIGHT > 64) ? compactThirdLine : moreCompactThirdLine)), "BT off");
    }

    // === Third & Fourth Rows: Node Identity ===
    int textWidth = 0;
    int nameX = 0;
    int yOffset = (SCREEN_WIDTH > 128) ? 0 : 7;
    const char *longName = nullptr;
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (ourNode && ourNode->has_user && strlen(ourNode->user.long_name) > 0) {
        longName = ourNode->user.long_name;
    }
    uint8_t dmac[6];
    char shortnameble[35];
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);
    snprintf(shortnameble, sizeof(shortnameble), "%s",
             graphics::UIRenderer::haveGlyphs(owner.short_name) ? owner.short_name : "");

    char combinedName[50];
    snprintf(combinedName, sizeof(combinedName), "%s (%s)", longName, shortnameble);
    if (SCREEN_WIDTH - (display->getStringWidth(longName) + display->getStringWidth(shortnameble)) > 10) {
        size_t len = strlen(combinedName);
        if (len >= 3 && strcmp(combinedName + len - 3, " ()") == 0) {
            combinedName[len - 3] = '\0'; // Remove the last three characters
        }
        textWidth = display->getStringWidth(combinedName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(
            nameX,
            ((rows == 4) ? compactThirdLine : ((SCREEN_HEIGHT > 64) ? compactFourthLine : moreCompactFourthLine)) + yOffset,
            combinedName);
    } else {
        textWidth = display->getStringWidth(longName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        yOffset = (strcmp(shortnameble, "") == 0) ? 1 : 0;
        if (yOffset == 1) {
            yOffset = (SCREEN_WIDTH > 128) ? 0 : 7;
        }
        display->drawString(
            nameX,
            ((rows == 4) ? compactThirdLine : ((SCREEN_HEIGHT > 64) ? compactFourthLine : moreCompactFourthLine)) + yOffset,
            longName);

        // === Fourth Row: ShortName Centered ===
        textWidth = display->getStringWidth(shortnameble);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX,
                            ((rows == 4) ? compactFourthLine : ((SCREEN_HEIGHT > 64) ? compactFifthLine : moreCompactFifthLine)),
                            shortnameble);
    }
}

// Start Functions to write date/time to the screen
// Helper function to check if a year is a leap year
bool isLeapYear(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

// Array of days in each month (non-leap year)
const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Fills the buffer with a formatted date/time string and returns pixel width
int formatDateTime(char *buf, size_t bufSize, uint32_t rtc_sec, OLEDDisplay *display, bool includeTime)
{
    int sec = rtc_sec % 60;
    rtc_sec /= 60;
    int min = rtc_sec % 60;
    rtc_sec /= 60;
    int hour = rtc_sec % 24;
    rtc_sec /= 24;

    int year = 1970;
    while (true) {
        int daysInYear = isLeapYear(year) ? 366 : 365;
        if (rtc_sec >= (uint32_t)daysInYear) {
            rtc_sec -= daysInYear;
            year++;
        } else {
            break;
        }
    }

    int month = 0;
    while (month < 12) {
        int dim = daysInMonth[month];
        if (month == 1 && isLeapYear(year))
            dim++;
        if (rtc_sec >= (uint32_t)dim) {
            rtc_sec -= dim;
            month++;
        } else {
            break;
        }
    }

    int day = rtc_sec + 1;

    if (includeTime) {
        snprintf(buf, bufSize, "%04d-%02d-%02d %02d:%02d:%02d", year, month + 1, day, hour, min, sec);
    } else {
        snprintf(buf, bufSize, "%04d-%02d-%02d", year, month + 1, day);
    }

    return display->getStringWidth(buf);
}

// Check if the display can render a string (detect special chars; emoji)
bool haveGlyphs(const char *str)
{
#if defined(OLED_PL) || defined(OLED_UA) || defined(OLED_RU) || defined(OLED_CS)
    // Don't want to make any assumptions about custom language support
    return true;
#endif

    // Check each character with the lookup function for the OLED library
    // We're not really meant to use this directly..
    bool have = true;
    for (uint16_t i = 0; i < strlen(str); i++) {
        uint8_t result = Screen::customFontTableLookup((uint8_t)str[i]);
        // If font doesn't support a character, it is substituted for ¿
        if (result == 191 && (uint8_t)str[i] != 191) {
            have = false;
            break;
        }
    }

    // LOG_DEBUG("haveGlyphs=%d", have);
    return have;
}

#ifdef USE_EINK
/// Used on eink displays while in deep sleep
void drawDeepSleepFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

    // Next frame should use full-refresh, and block while running, else device will sleep before async callback
    EINK_ADD_FRAMEFLAG(display, COSMETIC);
    EINK_ADD_FRAMEFLAG(display, BLOCKING);

    LOG_DEBUG("Draw deep sleep screen");

    // Display displayStr on the screen
    graphics::UIRenderer::drawIconScreen("Sleeping", display, state, x, y);
}

/// Used on eink displays when screen updates are paused
void drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    LOG_DEBUG("Draw screensaver overlay");

    EINK_ADD_FRAMEFLAG(display, COSMETIC); // Take the opportunity for a full-refresh

    // Config
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *pauseText = "Screen Paused";
    const char *idText = owner.short_name;
    const bool useId = haveGlyphs(idText); // This bool is used to hide the idText box if we can't render the short name
    constexpr uint16_t padding = 5;
    constexpr uint8_t dividerGap = 1;
    constexpr uint8_t imprecision = 5; // How far the box origins can drift from center. Combat burn-in.

    // Dimensions
    const uint16_t idTextWidth = display->getStringWidth(idText, strlen(idText), true); // "true": handle utf8 chars
    const uint16_t pauseTextWidth = display->getStringWidth(pauseText, strlen(pauseText));
    const uint16_t boxWidth = padding + (useId ? idTextWidth + padding + padding : 0) + pauseTextWidth + padding;
    const uint16_t boxHeight = padding + FONT_HEIGHT_SMALL + padding;

    // Position
    const int16_t boxLeft = (display->width() / 2) - (boxWidth / 2) + random(-imprecision, imprecision + 1);
    // const int16_t boxRight = boxLeft + boxWidth - 1;
    const int16_t boxTop = (display->height() / 2) - (boxHeight / 2 + random(-imprecision, imprecision + 1));
    const int16_t boxBottom = boxTop + boxHeight - 1;
    const int16_t idTextLeft = boxLeft + padding;
    const int16_t idTextTop = boxTop + padding;
    const int16_t pauseTextLeft = boxLeft + (useId ? padding + idTextWidth + padding : 0) + padding;
    const int16_t pauseTextTop = boxTop + padding;
    const int16_t dividerX = boxLeft + padding + idTextWidth + padding;
    const int16_t dividerTop = boxTop + 1 + dividerGap;
    const int16_t dividerBottom = boxBottom - 1 - dividerGap;

    // Draw: box
    display->setColor(EINK_WHITE);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Clear a slightly oversized area for the box
    display->setColor(EINK_BLACK);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);

    // Draw: Text
    if (useId)
        display->drawString(idTextLeft, idTextTop, idText);
    display->drawString(pauseTextLeft, pauseTextTop, pauseText);
    display->drawString(pauseTextLeft + 1, pauseTextTop, pauseText); // Faux bold

    // Draw: divider
    if (useId)
        display->drawLine(dividerX, dividerTop, dividerX, dividerBottom);
}
#endif

/**
 * Draw the icon with extra info printed around the corners
 */
void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *label = "BaseUI";
    display->setFont(FONT_SMALL);
    int textWidth = display->getStringWidth(label);
    int r = 3; // corner radius

    if (SCREEN_WIDTH > 128) {
        // === ORIGINAL WIDE SCREEN LAYOUT (unchanged) ===
        int padding = 4;
        int boxWidth = max(icon_width, textWidth) + (padding * 2) + 16;
        int boxHeight = icon_height + FONT_HEIGHT_SMALL + (padding * 3) - 8;
        int boxX = x - 1 + (SCREEN_WIDTH - boxWidth) / 2;
        int boxY = y - 6 + (SCREEN_HEIGHT - boxHeight) / 2;

        display->setColor(WHITE);
        display->fillRect(boxX + r, boxY, boxWidth - 2 * r, boxHeight);
        display->fillRect(boxX, boxY + r, boxWidth - 1, boxHeight - 2 * r);
        display->fillCircle(boxX + r, boxY + r, r);                                // Upper Left
        display->fillCircle(boxX + boxWidth - r - 1, boxY + r, r);                 // Upper Right
        display->fillCircle(boxX + r, boxY + boxHeight - r - 1, r);                // Lower Left
        display->fillCircle(boxX + boxWidth - r - 1, boxY + boxHeight - r - 1, r); // Lower Right

        display->setColor(BLACK);
        int iconX = boxX + (boxWidth - icon_width) / 2;
        int iconY = boxY + padding - 2;
        display->drawXbm(iconX, iconY, icon_width, icon_height, icon_bits);

        int labelY = iconY + icon_height + padding;
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + SCREEN_WIDTH / 2 - 3, labelY, label);
        display->drawString(x + SCREEN_WIDTH / 2 - 2, labelY, label); // faux bold

    } else {
        // === TIGHT SMALL SCREEN LAYOUT ===
        int iconY = y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 2;
        iconY -= 4;

        int labelY = iconY + icon_height - 2;

        int boxWidth = max(icon_width, textWidth) + 4;
        int boxX = x + (SCREEN_WIDTH - boxWidth) / 2;
        int boxY = iconY - 1;
        int boxBottom = labelY + FONT_HEIGHT_SMALL - 2;
        int boxHeight = boxBottom - boxY;

        display->setColor(WHITE);
        display->fillRect(boxX + r, boxY, boxWidth - 2 * r, boxHeight);
        display->fillRect(boxX, boxY + r, boxWidth - 1, boxHeight - 2 * r);
        display->fillCircle(boxX + r, boxY + r, r);
        display->fillCircle(boxX + boxWidth - r - 1, boxY + r, r);
        display->fillCircle(boxX + r, boxY + boxHeight - r - 1, r);
        display->fillCircle(boxX + boxWidth - r - 1, boxY + boxHeight - r - 1, r);

        display->setColor(BLACK);
        int iconX = boxX + (boxWidth - icon_width) / 2;
        display->drawXbm(iconX, iconY, icon_width, icon_height, icon_bits);

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + SCREEN_WIDTH / 2, labelY, label);
    }

    // === Footer and headers (shared) ===
    display->setFont(FONT_MEDIUM);
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = "meshtastic.org";
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);

    display->setFont(FONT_SMALL);
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    char buf[25];
    snprintf(buf, sizeof(buf), "%s\n%s", xstr(APP_VERSION_SHORT),
             graphics::UIRenderer::haveGlyphs(owner.short_name) ? owner.short_name : "");
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + SCREEN_WIDTH, y + 0, buf);

    screen->forceDisplay();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

// ****************************
// * My Position Screen       *
// ****************************
void drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Header ===
    graphics::drawCommonHeader(display, x, y);

    // === Draw title ===
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;
    const int textY = y + 1 + (highlightHeight - FONT_HEIGHT_SMALL) / 2;
    const char *titleStr = "GPS";
    const int centerX = x + SCREEN_WIDTH / 2;

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->setColor(BLACK);
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(centerX, textY, titleStr);
    if (config.display.heading_bold) {
        display->drawString(centerX + 1, textY, titleStr);
    }
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === First Row: My Location ===
#if HAS_GPS
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    const char *Satelite_String = "Sat:";
    display->drawString(0, ((SCREEN_HEIGHT > 64) ? compactFirstLine : moreCompactFirstLine), Satelite_String);
    const char *displayLine = ""; // Initialize to empty string by default
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        if (config.position.fixed_position) {
            displayLine = "Fixed GPS";
        } else {
            displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        }
        display->drawString(display->getStringWidth(Satelite_String) + 3,
                            ((SCREEN_HEIGHT > 64) ? compactFirstLine : moreCompactFirstLine), displayLine);
    } else {
        displayLine = "GPS enabled"; // Set a value when GPS is enabled
        UIRenderer::drawGps(display, display->getStringWidth(Satelite_String) + 3,
                            ((SCREEN_HEIGHT > 64) ? compactFirstLine : moreCompactFirstLine) + 3, gpsStatus);
    }

    config.display.heading_bold = origBold;

    // === Update GeoCoord ===
    geoCoord.updateCoords(int32_t(gpsStatus->getLatitude()), int32_t(gpsStatus->getLongitude()),
                          int32_t(gpsStatus->getAltitude()));

    // === Determine Compass Heading ===
    float heading;
    bool validHeading = false;

    if (screen->hasHeading()) {
        heading = radians(screen->getHeading());
        validHeading = true;
    } else {
        heading = screen->estimatedHeading(geoCoord.getLatitude() * 1e-7, geoCoord.getLongitude() * 1e-7);
        validHeading = !isnan(heading);
    }

    // If GPS is off, no need to display these parts
    if (strcmp(displayLine, "GPS off") != 0 && strcmp(displayLine, "No GPS") != 0) {

        // === Second Row: Altitude ===
        char DisplayLineTwo[32] = {0};
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            snprintf(DisplayLineTwo, sizeof(DisplayLineTwo), " Alt: %.0fft", geoCoord.getAltitude() * METERS_TO_FEET);
        } else {
            snprintf(DisplayLineTwo, sizeof(DisplayLineTwo), " Alt: %.0im", geoCoord.getAltitude());
        }
        display->drawString(x, ((SCREEN_HEIGHT > 64) ? compactSecondLine : moreCompactSecondLine), DisplayLineTwo);

        // === Third Row: Latitude ===
        char latStr[32];
        snprintf(latStr, sizeof(latStr), " Lat: %.5f", geoCoord.getLatitude() * 1e-7);
        display->drawString(x, ((SCREEN_HEIGHT > 64) ? compactThirdLine : moreCompactThirdLine), latStr);

        // === Fourth Row: Longitude ===
        char lonStr[32];
        snprintf(lonStr, sizeof(lonStr), " Lon: %.5f", geoCoord.getLongitude() * 1e-7);
        display->drawString(x, ((SCREEN_HEIGHT > 64) ? compactFourthLine : moreCompactFourthLine), lonStr);

        // === Fifth Row: Date ===
        uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
        char datetimeStr[25];
        bool showTime = false; // set to true for full datetime
        UIRenderer::formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display, showTime);
        char fullLine[40];
        snprintf(fullLine, sizeof(fullLine), " Date: %s", datetimeStr);
        display->drawString(0, ((SCREEN_HEIGHT > 64) ? compactFifthLine : moreCompactFifthLine), fullLine);
    }

    // === Draw Compass if heading is valid ===
    if (validHeading) {
        // --- Compass Rendering: landscape (wide) screens use original side-aligned logic ---
        if (SCREEN_WIDTH > SCREEN_HEIGHT) {
            const int16_t topY = compactFirstLine;
            const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1); // nav row height
            const int16_t usableHeight = bottomY - topY - 5;

            int16_t compassRadius = usableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            const int16_t compassDiam = compassRadius * 2;
            const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;

            // Center vertically and nudge down slightly to keep "N" clear of header
            const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

            CompassRenderer::drawNodeHeading(display, compassX, compassY, compassDiam, -heading);
            display->drawCircle(compassX, compassY, compassRadius);

            // "N" label
            float northAngle = -heading;
            float radius = compassRadius;
            int16_t nX = compassX + (radius - 1) * sin(northAngle);
            int16_t nY = compassY - (radius - 1) * cos(northAngle);
            int16_t nLabelWidth = display->getStringWidth("N") + 2;
            int16_t nLabelHeightBox = FONT_HEIGHT_SMALL + 1;

            display->setColor(BLACK);
            display->fillRect(nX - nLabelWidth / 2, nY - nLabelHeightBox / 2, nLabelWidth, nLabelHeightBox);
            display->setColor(WHITE);
            display->setFont(FONT_SMALL);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(nX, nY - FONT_HEIGHT_SMALL / 2, "N");
        } else {
            // Portrait or square: put compass at the bottom and centered, scaled to fit available space
            // For E-Ink screens, account for navigation bar at the bottom!
            int yBelowContent = ((SCREEN_HEIGHT > 64) ? compactFifthLine : moreCompactFifthLine) + FONT_HEIGHT_SMALL + 2;
            const int margin = 4;
            int availableHeight =
#if defined(USE_EINK)
                SCREEN_HEIGHT - yBelowContent - 24; // Leave extra space for nav bar on E-Ink
#else
                SCREEN_HEIGHT - yBelowContent - margin;
#endif

            if (availableHeight < FONT_HEIGHT_SMALL * 2)
                return;

            int compassRadius = availableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            if (compassRadius * 2 > SCREEN_WIDTH - 16)
                compassRadius = (SCREEN_WIDTH - 16) / 2;

            int compassX = x + SCREEN_WIDTH / 2;
            int compassY = yBelowContent + availableHeight / 2;

            CompassRenderer::drawNodeHeading(display, compassX, compassY, compassRadius * 2, -heading);
            display->drawCircle(compassX, compassY, compassRadius);

            // "N" label
            float northAngle = -heading;
            float radius = compassRadius;
            int16_t nX = compassX + (radius - 1) * sin(northAngle);
            int16_t nY = compassY - (radius - 1) * cos(northAngle);
            int16_t nLabelWidth = display->getStringWidth("N") + 2;
            int16_t nLabelHeightBox = FONT_HEIGHT_SMALL + 1;

            display->setColor(BLACK);
            display->fillRect(nX - nLabelWidth / 2, nY - nLabelHeightBox / 2, nLabelWidth, nLabelHeightBox);
            display->setColor(WHITE);
            display->setFont(FONT_SMALL);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(nX, nY - FONT_HEIGHT_SMALL / 2, "N");
        }
    }
#endif
}

#ifdef USERPREFS_OEM_TEXT

void drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    static const uint8_t xbm[] = USERPREFS_OEM_IMAGE_DATA;
    display->drawXbm(x + (SCREEN_WIDTH - USERPREFS_OEM_IMAGE_WIDTH) / 2,
                     y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - USERPREFS_OEM_IMAGE_HEIGHT) / 2 + 2, USERPREFS_OEM_IMAGE_WIDTH,
                     USERPREFS_OEM_IMAGE_HEIGHT, xbm);

    switch (USERPREFS_OEM_FONT_SIZE) {
    case 0:
        display->setFont(FONT_SMALL);
        break;
    case 2:
        display->setFont(FONT_LARGE);
        break;
    default:
        display->setFont(FONT_MEDIUM);
        break;
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = USERPREFS_OEM_TEXT;
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version and shortname in upper right
    char buf[25];
    snprintf(buf, sizeof(buf), "%s\n%s", xstr(APP_VERSION_SHORT), haveGlyphs(owner.short_name) ? owner.short_name : "");

    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + SCREEN_WIDTH, y + 0, buf);
    screen->forceDisplay();

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Restore left align, just to be kind to any other unsuspecting code
}

void drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawOEMIconScreen(region, display, state, x, y);
}

#endif

// Function overlay for showing mute/buzzer modifiers etc.
void drawFunctionOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // LOG_DEBUG("Draw function overlay");
    if (functionSymbol.begin() != functionSymbol.end()) {
        char buf[64];
        display->setFont(FONT_SMALL);
        snprintf(buf, sizeof(buf), "%s", functionSymbolString.c_str());
        display->drawString(SCREEN_WIDTH - display->getStringWidth(buf), SCREEN_HEIGHT - FONT_HEIGHT_SMALL, buf);
    }
}

// Navigation bar overlay implementation
static int8_t lastFrameIndex = -1;
static uint32_t lastFrameChangeTime = 0;
constexpr uint32_t ICON_DISPLAY_DURATION_MS = 2000;

void drawNavigationBar(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    int currentFrame = state->currentFrame;

    // Detect frame change and record time
    if (currentFrame != lastFrameIndex) {
        lastFrameIndex = currentFrame;
        lastFrameChangeTime = millis();
    }

    const bool useBigIcons = (SCREEN_WIDTH > 128);
    const int iconSize = useBigIcons ? 16 : 8;
    const int spacing = useBigIcons ? 8 : 4;
    const int bigOffset = useBigIcons ? 1 : 0;

    const size_t totalIcons = screen->indicatorIcons.size();
    if (totalIcons == 0)
        return;

    const size_t iconsPerPage = (SCREEN_WIDTH + spacing) / (iconSize + spacing);
    const size_t currentPage = currentFrame / iconsPerPage;
    const size_t pageStart = currentPage * iconsPerPage;
    const size_t pageEnd = min(pageStart + iconsPerPage, totalIcons);

    const int totalWidth = (pageEnd - pageStart) * iconSize + (pageEnd - pageStart - 1) * spacing;
    const int xStart = (SCREEN_WIDTH - totalWidth) / 2;

    // Only show bar briefly after switching frames (unless on E-Ink)
#if defined(USE_EINK)
    int y = SCREEN_HEIGHT - iconSize - 1;
#else
    int y = SCREEN_HEIGHT - iconSize - 1;
    if (millis() - lastFrameChangeTime > ICON_DISPLAY_DURATION_MS) {
        y = SCREEN_HEIGHT;
    }
#endif

    // Pre-calculate bounding rect
    const int rectX = xStart - 2 - bigOffset;
    const int rectWidth = totalWidth + 4 + (bigOffset * 2);
    const int rectHeight = iconSize + 6;

    // Clear background and draw border
    display->setColor(BLACK);
    display->fillRect(rectX + 1, y - 2, rectWidth - 2, rectHeight - 2);
    display->setColor(WHITE);
    display->drawRect(rectX, y - 2, rectWidth, rectHeight);

    // Icon drawing loop for the current page
    for (size_t i = pageStart; i < pageEnd; ++i) {
        const uint8_t *icon = screen->indicatorIcons[i];
        const int x = xStart + (i - pageStart) * (iconSize + spacing);
        const bool isActive = (i == static_cast<size_t>(currentFrame));

        if (isActive) {
            display->setColor(WHITE);
            display->fillRect(x - 2, y - 2, iconSize + 4, iconSize + 4);
            display->setColor(BLACK);
        }

        if (useBigIcons) {
            NodeListRenderer::drawScaledXBitmap16x16(x, y, 8, 8, icon, display);
        } else {
            display->drawXbm(x, y, iconSize, iconSize, icon);
        }

        if (isActive) {
            display->setColor(WHITE);
        }
    }

    // Knock the corners off the square
    display->setColor(BLACK);
    display->drawRect(rectX, y - 2, 1, 1);
    display->drawRect(rectX + rectWidth - 1, y - 2, 1, 1);
    display->setColor(WHITE);
}

void drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *message)
{
    uint16_t x_offset = display->width() / 2;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x_offset + x, 26 + y, message);
}

std::string drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds)
{
    std::string uptime;

    if (days > (HOURS_IN_MONTH * 6))
        uptime = "?";
    else if (days >= 2)
        uptime = std::to_string(days) + "d";
    else if (hours >= 2)
        uptime = std::to_string(hours) + "h";
    else if (minutes >= 1)
        uptime = std::to_string(minutes) + "m";
    else
        uptime = std::to_string(seconds) + "s";
    return uptime;
}

} // namespace UIRenderer
} // namespace graphics

#endif // !MESHTASTIC_EXCLUDE_GPS
#endif // HAS_SCREEN