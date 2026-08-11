#include "configuration.h"
#if HAS_SCREEN
#include "CompassRenderer.h"
#include "NodeDB.h"
#include "NodeListRenderer.h"
#if !MESHTASTIC_EXCLUDE_STATUS
#include "modules/StatusMessageModule.h"
#endif
#include "UIRenderer.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h" // for getTime() function
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/TouchLayout.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/images.h"
#include "meshUtils.h"
#include <algorithm>

// Global screen instance
extern std::unique_ptr<graphics::Screen> screen;

#if defined(OLED_TINY)
static uint32_t lastSwitchTime = 0;
#endif
namespace graphics
{
namespace NodeListRenderer
{

// Y position of the first row (either column) in the current list screen, set by
// drawNodeListScreen(). Used by entry renderers that need to special-case the top row.
static int16_t firstRowY = 0;

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
static ListMode_Node currentMode_Nodes = MODE_LAST_HEARD;
static ListMode_Location currentMode_Location = MODE_DISTANCE;
static int scrollIndex = 0;
// Popup overlay state
static uint32_t popupTime = 0;
static int popupTotal = 0;
static int popupStart = 0;
static int popupEnd = 0;
static int popupPage = 1;
static int popupMaxPage = 1;

static const uint32_t POPUP_DURATION_MS = 1000; // 1 second visible

// =============================
// Scrolling Logic
// =============================
void scrollUp()
{
    if (scrollIndex > 0)
        scrollIndex--;

    popupTime = millis(); // show popup
}

void scrollDown()
{
    scrollIndex++;
    popupTime = millis();
}

bool isTouchRowValid(uint32_t rowIndex)
{
    return nodeDB && rowIndex < static_cast<uint32_t>(nodeDB->getNumMeshNodes());
}

// =============================
// Utility Functions
// =============================

std::string getSafeNodeName(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int columnWidth)
{
    (void)display;
    (void)columnWidth;

    auto fallbackId = [&] {
        char id[12];
        std::snprintf(id, sizeof(id), "(%04X)", static_cast<uint16_t>(node ? (node->num & 0xFFFF) : 0));
        return std::string(id);
    };

    // 1) Choose target candidate (long vs short) only if present
    const char *raw = nullptr;

#if !MESHTASTIC_EXCLUDE_STATUS && !MESHTASTIC_EXCLUDE_STATUSDB
    // If long-name mode is enabled, and we have a recent status for this node,
    // prefer "(short_name) statusText" as the raw candidate. Pull straight out
    // of NodeDB's per-NodeNum cache instead of scanning a FIFO.
    std::string composedFromStatus;
    if (config.display.use_long_node_name && nodeInfoLiteHasUser(node) && nodeDB) {
        meshtastic_StatusMessage cachedStatus;
        if (nodeDB->copyNodeStatus(node->num, cachedStatus) && cachedStatus.status[0]) {
            const char *shortName = node->short_name;
            const size_t statusLen = std::strlen(cachedStatus.status);
            composedFromStatus.reserve(4 + (shortName ? std::strlen(shortName) : 0) + 1 + statusLen);
            composedFromStatus += "(";
            if (shortName && *shortName) {
                composedFromStatus += shortName;
            }
            composedFromStatus += ") ";
            composedFromStatus += cachedStatus.status;

            raw = composedFromStatus.c_str(); // safe for now; we'll sanitize immediately into std::string
        }
    }
#endif

    // If we didn't compose from status, use normal long/short selection
    if (!raw) {
        if (nodeInfoLiteHasUser(node)) {
            raw = config.display.use_long_node_name ? node->long_name : node->short_name;
        }
    }

    // 2) Preserve UTF-8 names so emotes can be detected and rendered.
    std::string nodeName = (raw && *raw) ? std::string(raw) : std::string{};
    if (nodeName.empty()) {
        nodeName = fallbackId();
    }

    return nodeName;
}

const char *getCurrentModeTitle_Nodes(int screenWidth)
{
    switch (currentMode_Nodes) {
    case MODE_LAST_HEARD:
        return "Last Heard";
    case MODE_HOP_SIGNAL:
#ifdef USE_EINK
        return "Hops/Sig";
#else
        return (currentResolution == ScreenResolution::High) ? "Hops/Signal" : "Hops/Sig";
#endif
    default:
        return "Nodes";
    }
}

const char *getCurrentModeTitle_Location(int screenWidth)
{
    switch (currentMode_Location) {
    case MODE_DISTANCE:
        return "Distance";
    case MODE_BEARING:
        return "Bearings";
    default:
        return "Nodes";
    }
}

static int getNodeNameMaxWidth(int columnWidth, int baseWidth)
{
    if (!config.display.use_long_node_name)
        return baseWidth;

    const int legacyLongNameWidth = columnWidth - ((currentResolution == ScreenResolution::High) ? 65 : 38);
    return std::max(0, std::min(baseWidth, legacyLongNameWidth));
}

// Use dynamic timing based on mode
unsigned long getModeCycleIntervalMs()
{
    return 3000;
}

void drawColumnSeparator(OLEDDisplay *display, int16_t x, int16_t yStart, int16_t yEnd)
{
    x = (currentResolution == ScreenResolution::High) ? x - 2 : (currentResolution == ScreenResolution::Low) ? x - 1 : x;
    for (int y = yStart; y <= yEnd; y += 2) {
        display->setPixel(x, y);
    }
}

void drawScrollbar(OLEDDisplay *display, int visibleNodeRows, int totalEntries, int scrollIndex, int columns, int scrollStartY)
{
    if (totalEntries <= visibleNodeRows * columns)
        return;

    int scrollbarHeight = display->getHeight() - scrollStartY - 10;
    int thumbHeight = max(4, (scrollbarHeight * visibleNodeRows * columns) / totalEntries);
    int thumbY = scrollStartY + (scrollIndex * (scrollbarHeight - thumbHeight)) /
                                    max(1, max(0, (totalEntries - 1) / (visibleNodeRows * columns)));

    int scrollbarX = display->getWidth() - 2;
    for (int i = 0; i < thumbHeight; i++) {
        display->setPixel(scrollbarX, thumbY + i);
    }
}

static inline void applyFavoriteNodeNameColor(OLEDDisplay *display, const meshtastic_NodeInfoLite *node, const char *nodeName,
                                              int16_t nameX, int16_t y, int nameMaxWidth)
{
    if (!display || !node || !nodeInfoLiteIsFavorite(node) || !isTFTColoringEnabled() || !nodeName) {
        return;
    }

    const int textWidth = UIRenderer::measureStringWithEmotes(display, nodeName);
    const int regionWidth = min(textWidth, max(0, nameMaxWidth));
    if (regionWidth <= 0) {
        return;
    }

    // Node list rows can begin a couple of pixels inside header space.
    // Clamp favorite-name color region below the header to avoid black overlap there.
    const int16_t minContentY = static_cast<int16_t>(FONT_HEIGHT_SMALL + 1);
    const int16_t regionY = max(y, minContentY);
    const int16_t yClip = regionY - y;
    const int16_t regionHeight = static_cast<int16_t>(FONT_HEIGHT_SMALL - yClip);
    if (regionHeight <= 0) {
        return;
    }

    setAndRegisterTFTColorRole(TFTColorRole::FavoriteNode, TFTPalette::Yellow, TFTPalette::Black, nameX, regionY, regionWidth,
                               regionHeight);
}

// =============================
// Entry Renderers
// =============================

void drawEntryLastHeard(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int nameMaxWidth = getNodeNameMaxWidth(columnWidth, columnWidth - 25);
    int timeOffset = (currentResolution == ScreenResolution::High) ? (isLeftCol ? 7 : 10) : (isLeftCol ? 3 : 7);

    const int nameX = x + ((currentResolution == ScreenResolution::High) ? 6 : 3);
    char nodeName[96];
    UIRenderer::truncateStringWithEmotes(display, getSafeNodeName(display, node, columnWidth).c_str(), nodeName, sizeof(nodeName),
                                         nameMaxWidth);
#if GRAPHICS_TFT_COLORING_ENABLED
    applyFavoriteNodeNameColor(display, node, nodeName, nameX, y, nameMaxWidth);
#endif
    bool isMuted = nodeInfoLiteIsMuted(node);

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
    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (nodeInfoLiteIsFavorite(node)) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (nodeInfoLiteIsIgnored(node) || isMuted) {
        if (currentResolution == ScreenResolution::High) {
            display->drawLine(x + 8, y + 8, (isLeftCol ? 0 : x - 4) + nameMaxWidth - 17, y + 8);
        } else {
            display->drawLine(x + 4, y + 6, (isLeftCol ? 0 : x - 3) + nameMaxWidth - 4, y + 6);
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

    int nameMaxWidth = getNodeNameMaxWidth(columnWidth, columnWidth - 25);
    int barsOffset = (currentResolution == ScreenResolution::High) ? (isLeftCol ? 20 : 24) : (isLeftCol ? 15 : 19);
    constexpr int kBarCount = 4;
    constexpr int kBarWidth = 2;
    constexpr int kBarGap = 1;

    int barsXOffset = columnWidth - barsOffset;
    int barsRightEdge = x + barsXOffset + ((kBarCount - 1) * (kBarWidth + kBarGap)) + kBarWidth;

    const int nameX = x + ((currentResolution == ScreenResolution::High) ? 6 : 3);
    char nodeName[96];
    UIRenderer::truncateStringWithEmotes(display, getSafeNodeName(display, node, columnWidth).c_str(), nodeName, sizeof(nodeName),
                                         nameMaxWidth);
#if GRAPHICS_TFT_COLORING_ENABLED
    applyFavoriteNodeNameColor(display, node, nodeName, nameX, y, nameMaxWidth);
#endif
    bool isMuted = nodeInfoLiteIsMuted(node);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (nodeInfoLiteIsFavorite(node)) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (nodeInfoLiteIsIgnored(node) || isMuted) {
        if (currentResolution == ScreenResolution::High) {
            display->drawLine(x + 8, y + 8, (isLeftCol ? 0 : x - 4) + nameMaxWidth - 17, y + 8);
        } else {
            display->drawLine(x + 4, y + 6, (isLeftCol ? 0 : x - 3) + nameMaxWidth - 4, y + 6);
        }
    }

    const bool isZeroHop = node->has_hops_away && node->hops_away == 0;

    // Show signal only for direct neighbors (0 hops)
    if (isZeroHop) {
        int bars = (node->snr > 5) ? 4 : (node->snr > 0) ? 3 : (node->snr > -5) ? 2 : (node->snr > -10) ? 1 : 0;
        int barStartX = x + barsXOffset;
        int barStartY = y + 1 + (FONT_HEIGHT_SMALL / 2) + 2;

        if (bars > 0) {
            uint16_t signalBarsColor = TFTPalette::Bad;
            if (bars >= 3) {
                signalBarsColor = TFTPalette::Good;
            } else if (bars == 2) {
                signalBarsColor = TFTPalette::Medium;
            }

            // Highest bar reaches 6 px in this renderer.
            setAndRegisterTFTColorRole(TFTColorRole::SignalBars, signalBarsColor, TFTPalette::Black, barStartX, barStartY - 6,
                                       (kBarCount * kBarWidth) + ((kBarCount - 1) * kBarGap), 6);
        }

        for (int b = 0; b < kBarCount; b++) {
            if (b < bars) {
                int height = (b * 2);
                display->fillRect(barStartX + (b * (kBarWidth + kBarGap)), barStartY - height, kBarWidth, height);
            }
        }
    }

    // Draw hop count + hop icon
    if (node->has_hops_away && node->hops_away > 0) {
        char hopCount[6];
        snprintf(hopCount, sizeof(hopCount), "%d", node->hops_away);

        const int hopCountWidth = display->getStringWidth(hopCount);
        const int gap = 1;
        const int totalWidth = hopCountWidth + gap + hop_width;
        const int hopX = barsRightEdge - totalWidth;

#if defined(BICOLOR_OLED_DISPLAY)
        int iconY = y + (FONT_HEIGHT_SMALL - hop_height) / 2;
        if (y == firstRowY) {
            iconY += 1; // Nudge the hop icon down 1px on the top row to avoid the two color display
        }
#else
        const int iconY = y + (FONT_HEIGHT_SMALL - hop_height) / 2;
#endif

        display->drawString(hopX, y, hopCount);
        display->drawXbm(hopX + hopCountWidth + gap, iconY, hop_width, hop_height, imghop);
    }
}

void drawNodeDistance(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int nameMaxWidth =
        getNodeNameMaxWidth(columnWidth, columnWidth - ((currentResolution == ScreenResolution::High) ? (isLeftCol ? 25 : 28)
                                                                                                      : (isLeftCol ? 20 : 22)));

    const int nameX = x + ((currentResolution == ScreenResolution::High) ? 6 : 3);
    char nodeName[96];
    UIRenderer::truncateStringWithEmotes(display, getSafeNodeName(display, node, columnWidth).c_str(), nodeName, sizeof(nodeName),
                                         nameMaxWidth);
#if GRAPHICS_TFT_COLORING_ENABLED
    applyFavoriteNodeNameColor(display, node, nodeName, nameX, y, nameMaxWidth);
#endif
    bool isMuted = nodeInfoLiteIsMuted(node);
    char distStr[10] = "";

    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourPos;
    meshtastic_PositionLite theirPos;
    const bool haveOurPos = ourNode && nodeDB->copyNodePosition(ourNode->num, ourPos);
    const bool haveTheirPos = nodeDB->copyNodePosition(node->num, theirPos);
    if (nodeDB->hasValidPosition(ourNode) && nodeDB->hasValidPosition(node) && haveOurPos && haveTheirPos) {
        double lat1 = ourPos.latitude_i * 1e-7;
        double lon1 = ourPos.longitude_i * 1e-7;
        double lat2 = theirPos.latitude_i * 1e-7;
        double lon2 = theirPos.longitude_i * 1e-7;

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
    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (nodeInfoLiteIsFavorite(node)) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (nodeInfoLiteIsIgnored(node) || isMuted) {
        if (currentResolution == ScreenResolution::High) {
            display->drawLine(x + 8, y + 8, (isLeftCol ? 0 : x - 4) + nameMaxWidth - 17, y + 8);
        } else {
            display->drawLine(x + 4, y + 6, (isLeftCol ? 0 : x - 3) + nameMaxWidth - 4, y + 6);
        }
    }

    const char *distanceLabel = (strlen(distStr) > 0) ? distStr : "?";
    int offset = (currentResolution == ScreenResolution::High)
                     ? (isLeftCol ? 7 : 10) // Offset for Wide Screens (Left Column:Right Column)
                     : (isLeftCol ? 4 : 7); // Offset for Narrow Screens (Left Column:Right Column)
    int rightEdge = x + columnWidth - offset;
    int textWidth = display->getStringWidth(distanceLabel);
    display->drawString(rightEdge - textWidth, y, distanceLabel);
}

void drawEntryDynamic_Nodes(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    switch (currentMode_Nodes) {
    case MODE_LAST_HEARD:
        drawEntryLastHeard(display, node, x, y, columnWidth);
        break;
    case MODE_HOP_SIGNAL:
        drawEntryHopSignal(display, node, x, y, columnWidth);
        break;
    default:
        break;
    }
}

void drawEntryCompass(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth)
{
    bool isLeftCol = (x < SCREEN_WIDTH / 2);

    // Adjust max text width depending on column and screen width
    int nameMaxWidth =
        getNodeNameMaxWidth(columnWidth, columnWidth - ((currentResolution == ScreenResolution::High) ? (isLeftCol ? 25 : 28)
                                                                                                      : (isLeftCol ? 20 : 22)));

    const int nameX = x + ((currentResolution == ScreenResolution::High) ? 6 : 3);
    char nodeName[96];
    UIRenderer::truncateStringWithEmotes(display, getSafeNodeName(display, node, columnWidth).c_str(), nodeName, sizeof(nodeName),
                                         nameMaxWidth);
#if GRAPHICS_TFT_COLORING_ENABLED
    applyFavoriteNodeNameColor(display, node, nodeName, nameX, y, nameMaxWidth);
#endif
    bool isMuted = nodeInfoLiteIsMuted(node);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (nodeInfoLiteIsFavorite(node)) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (nodeInfoLiteIsIgnored(node) || isMuted) {
        if (currentResolution == ScreenResolution::High) {
            display->drawLine(x + 8, y + 8, (isLeftCol ? 0 : x - 4) + nameMaxWidth - 17, y + 8);
        } else {
            display->drawLine(x + 4, y + 6, (isLeftCol ? 0 : x - 3) + nameMaxWidth - 4, y + 6);
        }
    }
}

void drawCompassArrow(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth,
                      float myHeadingRadian, double userLat, double userLon)
{
    if (!nodeDB->hasValidPosition(node))
        return;

    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int arrowXOffset = (currentResolution == ScreenResolution::High) ? (isLeftCol ? 22 : 24) : (isLeftCol ? 12 : 18);

    int centerX = x + columnWidth - arrowXOffset;
    int centerY = y + FONT_HEIGHT_SMALL / 2;

    meshtastic_PositionLite nodePos;
    if (!nodeDB->copyNodePosition(node->num, nodePos))
        return;
    double nodeLat = nodePos.latitude_i * 1e-7;
    double nodeLon = nodePos.longitude_i * 1e-7;
    float bearing = GeoCoord::bearing(userLat, userLon, nodeLat, nodeLon);
    float relativeBearing = CompassRenderer::adjustBearingForCompassMode(bearing, myHeadingRadian);
    float relativeBearingDeg = CompassRenderer::radiansToDegrees360(relativeBearing);
    // Shrink size by 2px
    int size = FONT_HEIGHT_SMALL - 5;
    CompassRenderer::drawArrowToNode(display, centerX, centerY, size, relativeBearingDeg);
    /*
    float angle = relativeBearing * DEG_TO_RAD;
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
    */
}

void drawCompassUnknown(OLEDDisplay *display, meshtastic_NodeInfoLite *node, int16_t x, int16_t y, int columnWidth, float, double,
                        double)
{
    if (!nodeDB->hasValidPosition(node))
        return;

    bool isLeftCol = (x < SCREEN_WIDTH / 2);
    int arrowXOffset = (currentResolution == ScreenResolution::High) ? (isLeftCol ? 22 : 24) : (isLeftCol ? 12 : 18);
    int centerX = x + columnWidth - arrowXOffset;

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(centerX, y, "?");
}

// =============================
// Main Screen Functions
// =============================

#if defined(T_DECK_PRO) && defined(USE_EINK)
static void drawNodeListScrollPopup(OLEDDisplay *display, int totalEntries, int startIndex, int perPage, int page,
                                    int usableTop, int usableBottom)
{
    if (millis() - popupTime >= POPUP_DURATION_MS || perPage <= 0)
        return;

    popupTotal = totalEntries;
    popupStart = totalEntries > 0 ? startIndex + 1 : 0;
    popupEnd = totalEntries > 0 ? min(startIndex + perPage, totalEntries) : 0;
    popupPage = page + 1;
    popupMaxPage = max(1, (totalEntries + perPage - 1) / perPage);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d-%d/%d  Pg %d/%d", popupStart, popupEnd, popupTotal, popupPage, popupMaxPage);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const int padding = 2;
    display->setFont(FONT_SMALL);
    const int textW = display->getStringWidth(buf);
    const int textH = FONT_HEIGHT_SMALL;
    const int boxWidth = textW + padding * 3;
    const int boxHeight = textH + padding * 2;
    const int usableHeight = max(1, usableBottom - usableTop);
    const int boxLeft = (display->getWidth() - boxWidth) / 2;
    const int boxTop = usableTop + (usableHeight - boxHeight) / 2;

    display->setColor(BLACK);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2);
    display->fillRect(boxLeft, boxTop - 2, boxWidth, 1);
    display->fillRect(boxLeft, boxTop + boxHeight + 1, boxWidth, 1);
    display->fillRect(boxLeft - 2, boxTop, 1, boxHeight);
    display->fillRect(boxLeft + boxWidth + 1, boxTop, 1, boxHeight);
    display->setColor(WHITE);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);
    display->setColor(BLACK);
    display->fillRect(boxLeft, boxTop, 1, 1);
    display->fillRect(boxLeft + boxWidth - 1, boxTop, 1, 1);
    display->fillRect(boxLeft, boxTop + boxHeight - 1, 1, 1);
    display->fillRect(boxLeft + boxWidth - 1, boxTop + boxHeight - 1, 1, 1);
    display->setColor(WHITE);
#if GRAPHICS_TFT_COLORING_ENABLED
    registerTFTActionMenuRegions(boxLeft, boxTop, boxWidth, boxHeight);
#endif
    display->drawString(boxLeft + padding, boxTop + padding, buf);
}

enum class TDeckNodeListMode : uint8_t { LastHeard, HopSignal, Distance, Bearings };

static void formatTDeckNodeAge(const meshtastic_NodeInfoLite *node, char *out, size_t outSize)
{
    const uint32_t seconds = sinceLastSeen(node);
    if (seconds == 0 || seconds == UINT32_MAX) {
        snprintf(out, outSize, "?");
        return;
    }

    const uint32_t minutes = seconds / 60;
    const uint32_t hours = minutes / 60;
    const uint32_t days = hours / 24;
    snprintf(out, outSize, (days > 365 ? "?" : "%lu%c"), static_cast<unsigned long>(days ? days : hours ? hours : minutes),
             days ? 'd' : hours ? 'h' : 'm');
}

static bool formatTDeckNodeDistance(const meshtastic_NodeInfoLite *node, char *out, size_t outSize)
{
    out[0] = '\0';
    if (!node || !nodeDB)
        return false;

    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourPos;
    meshtastic_PositionLite theirPos;
    if (!ourNode || !nodeDB->hasValidPosition(ourNode) || !nodeDB->hasValidPosition(node) ||
        !nodeDB->copyNodePosition(ourNode->num, ourPos) || !nodeDB->copyNodePosition(node->num, theirPos)) {
        return false;
    }

    const double lat1 = ourPos.latitude_i * 1e-7;
    const double lon1 = ourPos.longitude_i * 1e-7;
    const double lat2 = theirPos.latitude_i * 1e-7;
    const double lon2 = theirPos.longitude_i * 1e-7;
    const double dLat = (lat2 - lat1) * DEG_TO_RAD;
    const double dLon = (lon2 - lon1) * DEG_TO_RAD;
    const double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLon / 2) * sin(dLon / 2);
    const double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    const double distanceKm = 6371.0 * c;

    if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
        const double miles = distanceKm * 0.621371;
        if (miles < 0.1) {
            const int feet = static_cast<int>(miles * 5280);
            if (feet < 1000)
                snprintf(out, outSize, "%dft", feet);
            else
                snprintf(out, outSize, "1/4mi");
        } else {
            const int roundedMiles = static_cast<int>(miles + 0.5);
            if (roundedMiles < 1000)
                snprintf(out, outSize, "%dmi", roundedMiles);
            else
                snprintf(out, outSize, "999");
        }
    } else if (distanceKm < 1.0) {
        const int meters = static_cast<int>(distanceKm * 1000);
        if (meters < 1000)
            snprintf(out, outSize, "%dm", meters);
        else
            snprintf(out, outSize, "1k");
    } else {
        const int km = static_cast<int>(distanceKm + 0.5);
        if (km < 1000)
            snprintf(out, outSize, "%dk", km);
        else
            snprintf(out, outSize, "999");
    }
    return true;
}

static bool getTDeckNodeBearing(const meshtastic_NodeInfoLite *node, float headingRadian, float &degrees)
{
    if (!node || !nodeDB || !nodeDB->hasValidPosition(node))
        return false;

    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourPos;
    meshtastic_PositionLite nodePos;
    if (!ourNode || !nodeDB->hasValidPosition(ourNode) || !nodeDB->copyNodePosition(ourNode->num, ourPos) ||
        !nodeDB->copyNodePosition(node->num, nodePos)) {
        return false;
    }

    const float bearing = GeoCoord::bearing(DegD(ourPos.latitude_i), DegD(ourPos.longitude_i), DegD(nodePos.latitude_i),
                                            DegD(nodePos.longitude_i));
    const float relativeBearing = CompassRenderer::adjustBearingForCompassMode(bearing, headingRadian);
    degrees = CompassRenderer::radiansToDegrees360(relativeBearing);
    return true;
}

static void drawTDeckNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *title,
                                    TDeckNodeListMode mode, float headingRadian = 0.0f)
{
    (void)state;
    display->clear();
    graphics::drawCommonHeader(display, x, y, title);

    const int screenW = display->getWidth();
    const int screenH = display->getHeight();
    const int contentLeft = x + 8;
    const int contentRight = x + screenW - 8;
    const int summaryY = y + FONT_HEIGHT_SMALL + 5;
    const int separatorY = summaryY + FONT_HEIGHT_SMALL + 3;
    const int cardsTop = separatorY + 7;
    const int cardHeight = 50;
    const int cardGap = 4;
    const int footerReserve = (currentResolution == ScreenResolution::High) ? 24 : 16;
    const int bodyBottom = screenH - footerReserve;
    const int cardWidth = contentRight - contentLeft;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL_LOCAL);
    const bool locationMode = mode == TDeckNodeListMode::Distance || mode == TDeckNodeListMode::Bearings;

    std::vector<int> drawList;
    const int totalNodeEntries = nodeDB ? nodeDB->getNumMeshNodes() : 0;
    drawList.reserve(totalNodeEntries);
    for (int i = 0; i < totalNodeEntries; i++) {
        auto *node = nodeDB->getMeshNodeByIndex(i);
        if (node && node->num != nodeDB->getNodeNum() && (!locationMode || nodeDB->hasNodePosition(node->num)))
            drawList.push_back(node->num);
    }

    const int totalEntries = static_cast<int>(drawList.size());
    const int rowsAvailable = max(1, (bodyBottom - cardsTop + cardGap) / (cardHeight + cardGap));
    const int visibleRows = min(4, rowsAvailable);
    const int perPage = max(1, visibleRows);
    const int maxScroll = totalEntries > 0 ? max(0, (totalEntries - 1) / perPage) : 0;
    if (scrollIndex > maxScroll)
        scrollIndex = maxScroll;

    const int startIndex = scrollIndex * perPage;
    const int endIndex = min(startIndex + perPage, totalEntries);

    char rangeLabel[24];
    if (totalEntries > 0) {
        snprintf(rangeLabel, sizeof(rangeLabel), "%d-%d / %d", startIndex + 1, endIndex, totalEntries);
    } else {
        snprintf(rangeLabel, sizeof(rangeLabel), "0 / 0");
    }
    const char *summaryLabel = mode == TDeckNodeListMode::LastHeard   ? "RECENT NODES"
                               : mode == TDeckNodeListMode::HopSignal ? "SIGNAL / HOPS"
                               : mode == TDeckNodeListMode::Distance  ? "DISTANCE TABLE"
                                                                       : "BEARING TABLE";
    display->drawString(contentLeft, summaryY, summaryLabel);
    const int rangeWidth = display->getStringWidth(rangeLabel);
    display->drawString(contentRight - rangeWidth, summaryY, rangeLabel);
    display->drawLine(contentLeft, separatorY, contentRight, separatorY);

    if (totalEntries == 0) {
        display->setFont(FONT_SMALL);
        const char *emptyText = locationMode ? "No position data" : "No nodes heard yet";
        const int emptyWidth = display->getStringWidth(emptyText);
        display->drawString((screenW - emptyWidth) / 2, cardsTop + 30, emptyText);
    }

    for (int idx = startIndex; idx < endIndex; idx++) {
        auto *node = nodeDB->getMeshNode(drawList[idx]);
        if (!node)
            continue;

        const int row = idx - startIndex;
        const int cardY = cardsTop + row * (cardHeight + cardGap);
        display->drawRect(contentLeft, cardY, cardWidth, cardHeight);

        char age[10];
        formatTDeckNodeAge(node, age, sizeof(age));

        char metric[16];
        float bearingDegrees = 0.0f;
        bool hasBearing = false;
        switch (mode) {
        case TDeckNodeListMode::LastHeard:
            snprintf(metric, sizeof(metric), "%s", age);
            break;
        case TDeckNodeListMode::HopSignal:
            if (nodeInfoLiteHasSnr(node))
                snprintf(metric, sizeof(metric), "%+.1fdB", node->snr);
            else
                snprintf(metric, sizeof(metric), "--");
            break;
        case TDeckNodeListMode::Distance:
            if (!formatTDeckNodeDistance(node, metric, sizeof(metric)))
                snprintf(metric, sizeof(metric), "?");
            break;
        case TDeckNodeListMode::Bearings:
            hasBearing = getTDeckNodeBearing(node, headingRadian, bearingDegrees);
            if (hasBearing) {
                int bearing = static_cast<int>(bearingDegrees + 0.5f);
                if (bearing >= 360)
                    bearing = 0;
                snprintf(metric, sizeof(metric), "%03d", bearing);
            } else {
                snprintf(metric, sizeof(metric), "?");
            }
            break;
        }

        display->setFont(FONT_SMALL_LOCAL);
        const int metricWidth = display->getStringWidth(metric);
        const int nameX = contentLeft + 32;
        const int metricRight = mode == TDeckNodeListMode::Bearings ? contentRight - 48 : contentRight - 8;
        const int nameMaxWidth = max(24, metricRight - metricWidth - nameX - 8);
        display->setFont(FONT_SMALL);
        char nodeName[96];
        UIRenderer::truncateStringWithEmotes(display, getSafeNodeName(display, node, nameMaxWidth).c_str(), nodeName,
                                             sizeof(nodeName), nameMaxWidth);
#if GRAPHICS_TFT_COLORING_ENABLED
        applyFavoriteNodeNameColor(display, node, nodeName, nameX, cardY + 4, nameMaxWidth);
#endif
        UIRenderer::drawStringWithEmotes(display, nameX, cardY + 4, nodeName, FONT_HEIGHT_SMALL, 1, false);

        display->setFont(FONT_SMALL_LOCAL);
        display->drawString(metricRight - metricWidth, cardY + 6, metric);

        char shortName[16];
        if (nodeInfoLiteHasUser(node) && node->short_name[0]) {
            snprintf(shortName, sizeof(shortName), "%s", node->short_name);
        } else {
            snprintf(shortName, sizeof(shortName), "!%08lX", static_cast<unsigned long>(node->num));
        }

        char linkInfo[48];
        switch (mode) {
        case TDeckNodeListMode::LastHeard:
            if (nodeInfoLiteHasSnr(node))
                snprintf(linkInfo, sizeof(linkInfo), "%s  SNR %+.1f", shortName, node->snr);
            else
                snprintf(linkInfo, sizeof(linkInfo), "%s  SNR --", shortName);
            if (node->has_hops_away) {
                char withHops[48];
                snprintf(withHops, sizeof(withHops), "%s  H%u", linkInfo, node->hops_away);
                snprintf(linkInfo, sizeof(linkInfo), "%s", withHops);
            }
            break;
        case TDeckNodeListMode::HopSignal:
            snprintf(linkInfo, sizeof(linkInfo), "%s  HEARD %s", shortName, age);
            if (node->has_hops_away) {
                char withHops[48];
                snprintf(withHops, sizeof(withHops), "%s  H%u", linkInfo, node->hops_away);
                snprintf(linkInfo, sizeof(linkInfo), "%s", withHops);
            }
            break;
        case TDeckNodeListMode::Distance:
            snprintf(linkInfo, sizeof(linkInfo), "%s  HEARD %s", shortName, age);
            break;
        case TDeckNodeListMode::Bearings: {
            char distance[12];
            if (formatTDeckNodeDistance(node, distance, sizeof(distance)))
                snprintf(linkInfo, sizeof(linkInfo), "%s  %s", shortName, distance);
            else
                snprintf(linkInfo, sizeof(linkInfo), "%s  HEARD %s", shortName, age);
            break;
        }
        }
        display->drawString(nameX, cardY + 29, linkInfo);

        if (mode == TDeckNodeListMode::Bearings) {
            if (hasBearing)
                CompassRenderer::drawArrowToNode(display, contentRight - 24, cardY + FONT_HEIGHT_SMALL / 2, FONT_HEIGHT_SMALL - 5,
                                                  bearingDegrees);
            else {
                display->setTextAlignment(TEXT_ALIGN_CENTER);
                display->drawString(contentRight - 24, cardY + 7, "?");
                display->setTextAlignment(TEXT_ALIGN_LEFT);
            }
        }

        if (nodeInfoLiteIsFavorite(node)) {
            display->fillRect(contentLeft + 10, cardY + 35, 4, 4);
        }
        if (nodeInfoLiteIsIgnored(node) || nodeInfoLiteIsMuted(node)) {
            display->drawLine(nameX, cardY + 14, contentRight - 10, cardY + 14);
        }

        if (screen) {
            screen->addTouchTarget(touchExpandedRect(contentLeft, cardY, cardWidth, cardHeight, 2),
                                   meshtastic::TouchTargetKind::NodeRow, static_cast<uint32_t>(idx), INPUT_BROKER_NONE);
        }
    }

    drawCommonFooter(display, x, y);
    drawNodeListScrollPopup(display, totalEntries, startIndex, perPage, scrollIndex, summaryY, bodyBottom);
}
#endif

void drawNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *title,
                        EntryRenderer renderer, NodeExtrasRenderer extras, float headingRadian, double lat, double lon)
{
#if defined(T_DECK_PRO) && defined(USE_EINK)
    if (strcmp(title, "Last Heard") == 0) {
        drawTDeckNodeListScreen(display, state, x, y, title, TDeckNodeListMode::LastHeard);
        return;
    }
    if (strcmp(title, "Hops/Sig") == 0 || strcmp(title, "Hops / Signal") == 0) {
        drawTDeckNodeListScreen(display, state, x, y, title, TDeckNodeListMode::HopSignal);
        return;
    }
    if (strcmp(title, "Distance") == 0) {
        drawTDeckNodeListScreen(display, state, x, y, title, TDeckNodeListMode::Distance);
        return;
    }
    if (strcmp(title, "Bearings") == 0) {
        drawTDeckNodeListScreen(display, state, x, y, title, TDeckNodeListMode::Bearings, headingRadian);
        return;
    }
#endif
    const int COMMON_HEADER_HEIGHT = FONT_HEIGHT_SMALL - 1;
    const int rowYOffset = FONT_HEIGHT_SMALL - 3;
    bool locationScreen = false;

    if (strcmp(title, "Bearings") == 0)
        locationScreen = true;
    else if (strcmp(title, "Distance") == 0)
        locationScreen = true;
    display->clear();

    // Draw the battery/time header
    graphics::drawCommonHeader(display, x, y, title);

    // Space below header
    y += COMMON_HEADER_HEIGHT;
    firstRowY = y;

    int totalColumns = 1; // Default to 1 column

    if (config.display.use_long_node_name) {
        if (SCREEN_WIDTH <= 240) {
            totalColumns = 1;
        } else if (SCREEN_WIDTH > 240) {
            totalColumns = 2;
        }
    } else {
        if (SCREEN_WIDTH <= 64) {
            totalColumns = 1;
        } else if (SCREEN_WIDTH > 64 && SCREEN_WIDTH <= 240) {
            totalColumns = 2;
        } else {
            totalColumns = 3;
        }
    }

    int columnWidth = display->getWidth() / totalColumns;

    int totalEntries = nodeDB->getNumMeshNodes();
    int totalRowsAvailable = (display->getHeight() - y) / rowYOffset;
    int numskipped = 0;
    int visibleNodeRows = totalRowsAvailable;

    // Build filtered + ordered list
    std::vector<int> drawList;
    drawList.reserve(totalEntries);
    for (int i = 0; i < totalEntries; i++) {
        auto *n = nodeDB->getMeshNodeByIndex(i);

        if (!n)
            continue;
        if (n->num == nodeDB->getNodeNum())
            continue;
        if (locationScreen && !nodeDB->hasNodePosition(n->num))
            continue;

        drawList.push_back(n->num);
    }
    totalEntries = drawList.size();
    int perPage = visibleNodeRows * totalColumns;

    int maxScroll = 0;
    if (perPage > 0) {
        maxScroll = max(0, (totalEntries - 1) / perPage);
    }

    if (scrollIndex > maxScroll)
        scrollIndex = maxScroll;
    int startIndex = scrollIndex * visibleNodeRows * totalColumns;
    int endIndex = min(startIndex + visibleNodeRows * totalColumns, totalEntries);
    int yOffset = 0;
    int col = 0;
    int lastNodeY = y;
    int shownCount = 0;
    int rowCount = 0;

    for (int idx = startIndex; idx < endIndex; idx++) {
        uint32_t nodeNum = drawList[idx];
        auto *node = nodeDB->getMeshNode(nodeNum);
        int xPos = x + (col * columnWidth);
        int yPos = y + yOffset;

        renderer(display, node, xPos, yPos, columnWidth);

        if (extras)
            extras(display, node, xPos, yPos, columnWidth, headingRadian, lat, lon);

        if (screen) {
            screen->addTouchTarget(touchExpandedRect(xPos, yPos, columnWidth, rowYOffset, 2),
                                   meshtastic::TouchTargetKind::NodeRow, static_cast<uint32_t>(idx),
                                   INPUT_BROKER_NONE);
        }

        lastNodeY = max(lastNodeY, yPos + FONT_HEIGHT_SMALL);
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

    // This should correct the scrollbar
    totalEntries -= numskipped;

    // Draw column separator
    if (currentResolution != ScreenResolution::UltraLow && shownCount > 0) {
        const int firstNodeY = y + 3;
        for (int horizontal_offset = 1; horizontal_offset < totalColumns; horizontal_offset++) {
            drawColumnSeparator(display, columnWidth * horizontal_offset, firstNodeY, lastNodeY);
        }
    }

    const int scrollStartY = y + 3;
    drawScrollbar(display, visibleNodeRows, totalEntries, scrollIndex, totalColumns, scrollStartY);
    graphics::drawCommonFooter(display, x, y);

    // Scroll Popup Overlay
    if (millis() - popupTime < POPUP_DURATION_MS) {
        popupTotal = totalEntries;

        popupStart = startIndex + 1;
        popupEnd = min(startIndex + perPage, totalEntries);

        popupPage = (scrollIndex + 1);
        popupMaxPage = max(1, (totalEntries + perPage - 1) / perPage);

        char buf[32];
        snprintf(buf, sizeof(buf), "%d-%d/%d  Pg %d/%d", popupStart, popupEnd, popupTotal, popupPage, popupMaxPage);

        display->setTextAlignment(TEXT_ALIGN_LEFT);

        // Box padding
        int padding = 2;
        int textW = display->getStringWidth(buf);
        int textH = FONT_HEIGHT_SMALL;
        int boxWidth = textW + padding * 3;
        int boxHeight = textH + padding * 2;

        // Center of usable screen area:
        int headerHeight = FONT_HEIGHT_SMALL - 1;
        int footerHeight = FONT_HEIGHT_SMALL + 2;

        int usableTop = headerHeight;
        int usableBottom = display->getHeight() - footerHeight;
        int usableHeight = usableBottom - usableTop;

        // Center point inside usable area
        int boxLeft = (display->getWidth() - boxWidth) / 2;
        int boxTop = usableTop + (usableHeight - boxHeight) / 2;

        // Draw Box
        display->setColor(BLACK);
        display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2);
        display->fillRect(boxLeft, boxTop - 2, boxWidth, 1);
        display->fillRect(boxLeft, boxTop + boxHeight + 1, boxWidth, 1);
        display->fillRect(boxLeft - 2, boxTop, 1, boxHeight);
        display->fillRect(boxLeft + boxWidth + 1, boxTop, 1, boxHeight);
        display->setColor(WHITE);
        display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);
        display->setColor(BLACK);
        display->fillRect(boxLeft, boxTop, 1, 1);
        display->fillRect(boxLeft + boxWidth - 1, boxTop, 1, 1);
        display->fillRect(boxLeft, boxTop + boxHeight - 1, 1, 1);
        display->fillRect(boxLeft + boxWidth - 1, boxTop + boxHeight - 1, 1, 1);
        display->setColor(WHITE);
#if GRAPHICS_TFT_COLORING_ENABLED
        registerTFTActionMenuRegions(boxLeft, boxTop, boxWidth, boxHeight);
#endif

        // Text
        display->drawString(boxLeft + padding, boxTop + padding, buf);
    }
}

// =============================
// Screen Frame Functions
// =============================

#ifndef USE_EINK
// Node list for Last Heard and Hop Signal views
void drawDynamicListScreen_Nodes(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Static variables to track mode and duration
    static ListMode_Node lastRenderedMode = MODE_COUNT_NODE;
    static unsigned long modeStartTime = 0;

    unsigned long now = millis();

#if defined(OLED_TINY)
    display->clear();
    if (now - lastSwitchTime >= 3000) {
        display->display();
        lastSwitchTime = now;
    }
#endif
    // On very first call (on boot or state enter)
    if (lastRenderedMode == MODE_COUNT_NODE) {
        currentMode_Nodes = MODE_LAST_HEARD;
        modeStartTime = now;
    }

    // Time to switch to next mode?
    if (now - modeStartTime >= getModeCycleIntervalMs()) {
        currentMode_Nodes = static_cast<ListMode_Node>((currentMode_Nodes + 1) % MODE_COUNT_NODE);
        modeStartTime = now;
    }

    // Render screen based on currentMode
    const char *title = getCurrentModeTitle_Nodes(display->getWidth());
    drawNodeListScreen(display, state, x, y, title, drawEntryDynamic_Nodes);

    // Track the last mode to avoid reinitializing modeStartTime
    lastRenderedMode = currentMode_Nodes;
}

// Node list for Distance and Bearings views
void drawDynamicListScreen_Location(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Static variables to track mode and duration
    static ListMode_Location lastRenderedMode = MODE_COUNT_LOCATION;
    static unsigned long modeStartTime = 0;

    unsigned long now = millis();

#if defined(OLED_TINY)
    display->clear();
    if (now - lastSwitchTime >= 3000) {
        display->display();
        lastSwitchTime = now;
    }
#endif
    // On very first call (on boot or state enter)
    if (lastRenderedMode == MODE_COUNT_LOCATION) {
        currentMode_Location = MODE_DISTANCE;
        modeStartTime = now;
    }

    // Time to switch to next mode?
    if (now - modeStartTime >= getModeCycleIntervalMs()) {
        currentMode_Location = static_cast<ListMode_Location>((currentMode_Location + 1) % MODE_COUNT_LOCATION);
        modeStartTime = now;
    }

    // Render screen based on currentMode
    const char *title = getCurrentModeTitle_Location(display->getWidth());

    // Render screen based on currentMode_Location
    if (currentMode_Location == MODE_DISTANCE) {
        drawNodeListScreen(display, state, x, y, title, drawNodeDistance);
    } else if (currentMode_Location == MODE_BEARING) {
        drawNodeListWithCompasses(display, state, x, y);
    }

    // Track the last mode to avoid reinitializing modeStartTime
    lastRenderedMode = currentMode_Location;
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
#if defined(T_DECK_PRO) && defined(USE_EINK)
    const char *title = "Hops / Signal";
#elif defined(USE_EINK)
    const char *title = "Hops/Sig";
#else

    const char *title = "Hops/Signal";
#endif
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
    float headingRadian = 0.0f;
    const auto *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourSelfPos;
    if (!ourNode || !nodeDB->hasValidPosition(ourNode) || !nodeDB->copyNodePosition(ourNode->num, ourSelfPos)) {
        drawNodeListScreen(display, state, x, y, "Bearings", drawEntryCompass, drawCompassUnknown, headingRadian, 0.0, 0.0);
        return;
    }

    double lat = DegD(ourSelfPos.latitude_i);
    double lon = DegD(ourSelfPos.longitude_i);

#if defined(OLED_TINY)
    display->clear();
    uint32_t now = millis();
    if (now - lastSwitchTime >= 2000) {
        display->display();
        lastSwitchTime = now;
    }
#endif
    if (!CompassRenderer::getHeadingRadians(lat, lon, headingRadian)) {
        drawNodeListScreen(display, state, x, y, "Bearings", drawEntryCompass, drawCompassUnknown, headingRadian, lat, lon);
        return;
    }

    drawNodeListScreen(display, state, x, y, "Bearings", drawEntryCompass, drawCompassArrow, headingRadian, lat, lon);
}

} // namespace NodeListRenderer
} // namespace graphics
#endif
