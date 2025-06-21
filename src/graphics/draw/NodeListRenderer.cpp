#include "configuration.h"
#if HAS_SCREEN
#include "CompassRenderer.h"
#include "NodeDB.h"
#include "NodeListRenderer.h"
#include "UIRenderer.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h" // for getTime() function
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "meshUtils.h"
#include <algorithm>

// Forward declarations for functions defined in Screen.cpp
namespace graphics
{
extern bool haveGlyphs(const char *str);
} // namespace graphics

// Global screen instance
extern graphics::Screen *screen;

namespace graphics
{
namespace NodeListRenderer
{

// Function moved from Screen.cpp to NodeListRenderer.cpp since it's primarily used here
void drawScaledXBitmap16x16(int x, int y, int width, int height, const uint8_t *bitmapXBM, OLEDDisplay *display)
{
    for (int row = 0; row < height; row++) {
        uint8_t rowMask = (1 << row);
        for (int col = 0; col < width; col++) {
            uint8_t colData = pgm_read_byte(&bitmapXBM[col]);
            if (colData & rowMask) {
                // Note: rows become X, columns become Y after transpose
                display->fillRect(x + row * 2, y + col * 2, 2, 2);
            }
        }
    }
}

// Static variables for dynamic cycling
static NodeListMode currentMode = MODE_LAST_HEARD;
static int scrollIndex = 0;

// =============================
// Utility Functions
// =============================

const char *getSafeNodeName(meshtastic_NodeInfoLite *node)
{
    static char nodeName[16] = "?";
    if (node->has_user && strlen(node->user.short_name) > 0) {
        bool valid = true;
        const char *name = node->user.short_name;
        for (size_t i = 0; i < strlen(name); i++) {
            uint8_t c = (uint8_t)name[i];
            if (c < 32 || c > 126) {
                valid = false;
                break;
            }
        }
        if (valid) {
            strncpy(nodeName, name, sizeof(nodeName) - 1);
            nodeName[sizeof(nodeName) - 1] = '\0';
        } else {
            snprintf(nodeName, sizeof(nodeName), "%04X", (uint16_t)(node->num & 0xFFFF));
        }
    } else {
        strcpy(nodeName, "?");
    }
    return nodeName;
}

const char *getCurrentModeTitle(int screenWidth)
{
    switch (currentMode) {
    case MODE_LAST_HEARD:
        return "Last Heard";
    case MODE_HOP_SIGNAL:
        return (screenWidth > 128) ? "Hops/Signal" : "Hops/Sig";
    case MODE_DISTANCE:
        return "Distance";
    default:
        return "Nodes";
    }
}

// Use dynamic timing based on mode
unsigned long getModeCycleIntervalMs()
{
    return 3000;
}

// Calculate bearing between two lat/lon points
float calculateBearing(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double y = sin(dLon) * cos(lat2 * DEG_TO_RAD);
    double x = cos(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) - sin(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(dLon);
    double bearing = atan2(y, x) * RAD_TO_DEG;
    return fmod(bearing + 360.0, 360.0);
}

int calculateMaxScroll(int totalEntries, int visibleRows)
{
    return std::max(0, (totalEntries - 1) / (visibleRows * 2));
}

void retrieveAndSortNodes(std::vector<NodeEntry> &nodeList)
{
    size_t numNodes = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < numNodes; i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == nodeDB->getNodeNum())
            continue;

        NodeEntry entry;
        entry.node = node;
        entry.sortValue = sinceLastSeen(node);

        nodeList.push_back(entry);
    }

    // Sort nodes: favorites first, then by last heard (most recent first)
    std::sort(nodeList.begin(), nodeList.end(), [](const NodeEntry &a, const NodeEntry &b) {
        bool aFav = a.node->is_favorite;
        bool bFav = b.node->is_favorite;
        if (aFav != bFav)
            return aFav;
        if (a.sortValue == 0 || a.sortValue == UINT32_MAX)
            return false;
        if (b.sortValue == 0 || b.sortValue == UINT32_MAX)
            return true;
        return a.sortValue < b.sortValue;
    });
}

void drawColumnSeparator(OLEDDisplay *display, int16_t x, int16_t yStart, int16_t yEnd)
{
    int columnWidth = display->getWidth() / 2;
    int separatorX = x + columnWidth - 2;
    for (int y = yStart; y <= yEnd; y += 2) {
        display->setPixel(separatorX, y);
    }
}

void drawScrollbar(OLEDDisplay *display, int visibleNodeRows, int totalEntries, int scrollIndex, int columns, int scrollStartY)
{
    if (totalEntries <= visibleNodeRows * columns)
        return;

    int scrollbarX = display->getWidth() - 2;
    int scrollbarHeight = display->getHeight() - scrollStartY - 10;
    int thumbHeight = std::max(4, (scrollbarHeight * visibleNodeRows * columns) / totalEntries);
    int maxScroll = calculateMaxScroll(totalEntries, visibleNodeRows);
    int thumbY = scrollStartY + (scrollIndex * (scrollbarHeight - thumbHeight)) / std::max(1, maxScroll);

    for (int i = 0; i < thumbHeight; i++) {
        display->setPixel(scrollbarX, thumbY + i);
    }
}

// =============================
// Entry Renderers
// =============================

void drawEntryLastHeard(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int timeOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 7 : 10) : (isLeftCol ? 3 : 7);

    const char *nodeName = getSafeNodeName(node);

    char timeStr[10];
    uint32_t seconds = sinceLastSeen(node);
    if (seconds == 0 || seconds == UINT32_MAX) {
        snprintf(timeStr, sizeof(timeStr), "?");
    } else {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        snprintf(timeStr, sizeof(timeStr), (days > 365 ? "?" : "%d%c"),
                 (days    ? days
                  : hours ? hours
                          : minutes),
                 (days    ? 'd'
                  : hours ? 'h'
                          : 'm'));
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(x + ((SCREEN_WIDTH > 128) ? 6 : 3), y, nodeName);
    if (node->is_favorite) {
        if (SCREEN_WIDTH > 128) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }

    int rightEdge = x + columnWidth - timeOffset;
    if (timeStr[strlen(timeStr) - 1] == 'm') // Fix the fact that our fonts don't line up well all the time
        rightEdge -= 1;
    int textWidth = display->getStringWidth(timeStr);
    display->drawString(rightEdge - textWidth, y, timeStr);
}

void drawEntryHopSignal(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);

    int nameMaxWidth = columnWidth - 25;
    int barsOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 20 : 24) : (isLeftCol ? 15 : 19);
    int hopOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 21 : 29) : (isLeftCol ? 13 : 17);

    int barsXOffset = columnWidth - barsOffset;

    const char *nodeName = getSafeNodeName(node);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    display->drawStringMaxWidth(x + ((SCREEN_WIDTH > 128) ? 6 : 3), y, nameMaxWidth, nodeName);
    if (node->is_favorite) {
        if (SCREEN_WIDTH > 128) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }

    // Draw signal strength bars
    int bars = (node->snr > 5) ? 4 : (node->snr > 0) ? 3 : (node->snr > -5) ? 2 : (node->snr > -10) ? 1 : 0;
    int barWidth = 2;
    int barStartX = x + barsXOffset;
    int barStartY = y + 1 + (FONT_HEIGHT_SMALL / 2) + 2;

    for (int b = 0; b < 4; b++) {
        if (b < bars) {
            int height = (b * 2);
            display->fillRect(barStartX + (b * (barWidth + 1)), barStartY - height, barWidth, height);
        }
    }

    // Draw hop count
    char hopStr[6] = "";
    if (node->has_hops_away && node->hops_away > 0)
        snprintf(hopStr, sizeof(hopStr), "[%d]", node->hops_away);

    if (hopStr[0] != '\0') {
        int rightEdge = x + columnWidth - hopOffset;
        int textWidth = display->getStringWidth(hopStr);
        display->drawString(rightEdge - textWidth, y, hopStr);
    }
}

void drawNodeDistance(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int nameMaxWidth = columnWidth - (SCREEN_WIDTH > 128 ? (isLeftCol ? 25 : 28) : (isLeftCol ? 20 : 22));

    const char *nodeName = getSafeNodeName(node);
    char distStr[10] = "";

    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
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
                if (feet < 1000)
                    snprintf(distStr, sizeof(distStr), "%dft", feet);
                else
                    snprintf(distStr, sizeof(distStr), "¼mi"); // 4-char max
            } else {
                int roundedMiles = (int)(miles + 0.5);
                if (roundedMiles < 1000)
                    snprintf(distStr, sizeof(distStr), "%dmi", roundedMiles);
                else
                    snprintf(distStr, sizeof(distStr), "999"); // Max display cap
            }
        } else {
            if (distanceKm < 1.0) {
                int meters = (int)(distanceKm * 1000);
                if (meters < 1000)
                    snprintf(distStr, sizeof(distStr), "%dm", meters);
                else
                    snprintf(distStr, sizeof(distStr), "1k");
            } else {
                int km = (int)(distanceKm + 0.5);
                if (km < 1000)
                    snprintf(distStr, sizeof(distStr), "%dk", km);
                else
                    snprintf(distStr, sizeof(distStr), "999");
            }
        }
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawStringMaxWidth(x + ((SCREEN_WIDTH > 128) ? 6 : 3), y, nameMaxWidth, nodeName);
    if (node->is_favorite) {
        if (SCREEN_WIDTH > 128) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }

    if (strlen(distStr) > 0) {
        int offset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 7 : 10) // Offset for Wide Screens (Left Column:Right Column)
                                          : (isLeftCol ? 4 : 7); // Offset for Narrow Screens (Left Column:Right Column)
        int rightEdge = x + columnWidth - offset;
        int textWidth = display->getStringWidth(distStr);
        display->drawString(rightEdge - textWidth, y, distStr);
    }
}

void drawEntryDynamic(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    switch (currentMode) {
    case MODE_LAST_HEARD:
        drawEntryLastHeard(display, node, x, y, columnWidth);
        break;
    case MODE_HOP_SIGNAL:
        drawEntryHopSignal(display, node, x, y, columnWidth);
        break;
    case MODE_DISTANCE:
        drawNodeDistance(display, node, x, y, columnWidth);
        break;
    default:
        break;
    }
}

void drawEntryCompass(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);

    // Adjust max text width depending on column and screen width
    int nameMaxWidth = columnWidth - (SCREEN_WIDTH > 128 ? (isLeftCol ? 25 : 28) : (isLeftCol ? 20 : 22));

    const char *nodeName = getSafeNodeName(node);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawStringMaxWidth(x + ((SCREEN_WIDTH > 128) ? 6 : 3), y, nameMaxWidth, nodeName);
    if (node->is_favorite) {
        if (SCREEN_WIDTH > 128) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
}

void drawCompassArrow(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth, float myHeading,
                      double userLat, double userLon)
{
    if (!nodeDB->hasValidPosition(node))
        return;

    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int arrowXOffset = (SCREEN_WIDTH > 128) ? (isLeftCol ? 22 : 24) : (isLeftCol ? 12 : 18);

    int centerX = x + columnWidth - arrowXOffset;
    int centerY = y + FONT_HEIGHT_SMALL / 2;

    double nodeLat = node->position.latitude_i * 1e-7;
    double nodeLon = node->position.longitude_i * 1e-7;
    float bearingToNode = calculateBearing(userLat, userLon, nodeLat, nodeLon);
    float relativeBearing = fmod((bearingToNode - myHeading + 360), 360);
    float angle = relativeBearing * DEG_TO_RAD;

    // Shrink size by 2px
    int size = FONT_HEIGHT_SMALL - 5;
    float halfSize = size / 2.0;

    // Point of the arrow
    int tipX = centerX + halfSize * cos(angle);
    int tipY = centerY - halfSize * sin(angle);

    float baseAngle = radians(35);
    float sideLen = halfSize * 0.95;
    float notchInset = halfSize * 0.35;

    // Left and right corners
    int leftX = centerX + sideLen * cos(angle + PI - baseAngle);
    int leftY = centerY - sideLen * sin(angle + PI - baseAngle);

    int rightX = centerX + sideLen * cos(angle + PI + baseAngle);
    int rightY = centerY - sideLen * sin(angle + PI + baseAngle);

    // Center notch (cut-in)
    int notchX = centerX - notchInset * cos(angle);
    int notchY = centerY + notchInset * sin(angle);

    // Draw the chevron-style arrowhead
    display->fillTriangle(tipX, tipY, leftX, leftY, notchX, notchY);
    display->fillTriangle(tipX, tipY, notchX, notchY, rightX, rightY);
}

// =============================
// Main Screen Functions
// =============================

void drawNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *title,
                        EntryRenderer renderer, NodeExtrasRenderer extras, float heading, double lat, double lon)
{
    const int COMMON_HEADER_HEIGHT = FONT_HEIGHT_SMALL - 1;
    const int rowYOffset = FONT_HEIGHT_SMALL - 3;

    int columnWidth = display->getWidth() / 2;

    display->clear();

    // Draw the battery/time header
    graphics::drawCommonHeader(display, x, y, title);

    // Space below header
    y += COMMON_HEADER_HEIGHT;

    // Fetch and display sorted node list
    std::vector<NodeEntry> nodeList;
    retrieveAndSortNodes(nodeList);

    int totalEntries = nodeList.size();
    int totalRowsAvailable = (display->getHeight() - y) / rowYOffset;
#ifdef USE_EINK
    totalRowsAvailable -= 1;
#endif
    int visibleNodeRows = totalRowsAvailable;
    int totalColumns = 2;

    int startIndex = scrollIndex * visibleNodeRows * totalColumns;
    int endIndex = std::min(startIndex + visibleNodeRows * totalColumns, totalEntries);

    int yOffset = 0;
    int col = 0;
    int lastNodeY = y;
    int shownCount = 0;
    int rowCount = 0;

    for (int i = startIndex; i < endIndex; ++i) {
        int xPos = x + (col * columnWidth);
        int yPos = y + yOffset;
        renderer(display, nodeList[i].node, xPos, yPos, columnWidth);

        if (extras) {
            extras(display, nodeList[i].node, xPos, yPos, columnWidth, heading, lat, lon);
        }

        lastNodeY = std::max(lastNodeY, yPos + FONT_HEIGHT_SMALL);
        yOffset += rowYOffset;
        shownCount++;
        rowCount++;

        if (rowCount >= totalRowsAvailable) {
            yOffset = 0;
            rowCount = 0;
            col++;
            if (col > (totalColumns - 1))
                break;
        }
    }

    // Draw column separator
    if (shownCount > 0) {
        const int firstNodeY = y + 3;
        drawColumnSeparator(display, x, firstNodeY, lastNodeY);
    }

    const int scrollStartY = y + 3;
    drawScrollbar(display, visibleNodeRows, totalEntries, scrollIndex, 2, scrollStartY);
}

// =============================
// Screen Frame Functions
// =============================

#ifndef USE_EINK
void drawDynamicNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Static variables to track mode and duration
    static NodeListMode lastRenderedMode = MODE_COUNT;
    static unsigned long modeStartTime = 0;

    unsigned long now = millis();

    // On very first call (on boot or state enter)
    if (lastRenderedMode == MODE_COUNT) {
        currentMode = MODE_LAST_HEARD;
        modeStartTime = now;
    }

    // Time to switch to next mode?
    if (now - modeStartTime >= getModeCycleIntervalMs()) {
        currentMode = static_cast<NodeListMode>((currentMode + 1) % MODE_COUNT);
        modeStartTime = now;
    }

    // Render screen based on currentMode
    const char *title = getCurrentModeTitle(display->getWidth());
    drawNodeListScreen(display, state, x, y, title, drawEntryDynamic);

    // Track the last mode to avoid reinitializing modeStartTime
    lastRenderedMode = currentMode;
}
#endif

#ifdef USE_EINK
void drawLastHeardScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *title = "Last Heard";
    drawNodeListScreen(display, state, x, y, title, drawEntryLastHeard);
}

void drawHopSignalScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *title = "Hops/Signal";
    drawNodeListScreen(display, state, x, y, title, drawEntryHopSignal);
}

void drawDistanceScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const char *title = "Distance";
    drawNodeListScreen(display, state, x, y, title, drawNodeDistance);
}
#endif

void drawNodeListWithCompasses(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    float heading = 0;
    bool validHeading = false;
    double lat = 0;
    double lon = 0;

#if HAS_GPS
    if (screen->hasHeading()) {
        heading = screen->getHeading(); // degrees
        validHeading = true;
    } else {
        heading = screen->estimatedHeading(lat, lon);
        validHeading = !isnan(heading);
    }
#endif

    if (!validHeading)
        return;

    drawNodeListScreen(display, state, x, y, "Bearings", drawEntryCompass, drawCompassArrow, heading, lat, lon);
}

/// Draw a series of fields in a column, wrapping to multiple columns if needed
void drawColumns(OLEDDisplay *display, int16_t x, int16_t y, const char **fields)
{
    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    const char **f = fields;
    int xo = x, yo = y;
    while (*f) {
        display->drawString(xo, yo, *f);
        if ((display->getColor() == BLACK) && config.display.heading_bold)
            display->drawString(xo + 1, yo, *f);

        display->setColor(WHITE);
        yo += FONT_HEIGHT_SMALL;
        if (yo > SCREEN_HEIGHT - FONT_HEIGHT_SMALL) {
            xo += SCREEN_WIDTH / 2;
            yo = 0;
        }
        f++;
    }
}

} // namespace NodeListRenderer
} // namespace graphics
#endif