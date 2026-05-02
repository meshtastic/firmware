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
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
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

#if defined(M5STACK_UNITC6L)
static uint32_t lastSwitchTime = 0;
#endif
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

#if !MESHTASTIC_EXCLUDE_STATUS
    // If long-name mode is enabled, and we have a recent status for this node,
    // prefer "(short_name) statusText" as the raw candidate.
    std::string composedFromStatus;
    if (config.display.use_long_node_name && node && node->has_user && statusMessageModule) {
        const auto &recent = statusMessageModule->getRecentReceived();
        const StatusMessageModule::RecentStatus *found = nullptr;
        for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
            if (it->fromNodeId == node->num && !it->statusText.empty()) {
                found = &(*it);
                break;
            }
        }

        if (found) {
            const char *shortName = node->user.short_name;
            composedFromStatus.reserve(4 + (shortName ? std::strlen(shortName) : 0) + 1 + found->statusText.size());
            composedFromStatus += "(";
            if (shortName && *shortName) {
                composedFromStatus += shortName;
            }
            composedFromStatus += ") ";
            composedFromStatus += found->statusText;

            raw = composedFromStatus.c_str(); // safe for now; we'll sanitize immediately into std::string
        }
    }
#endif

    // If we didn't compose from status, use normal long/short selection
    if (!raw) {
        if (node && node->has_user) {
            raw = config.display.use_long_node_name ? node->user.long_name : node->user.short_name;
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

int calculateMaxScroll(int totalEntries, int visibleRows)
{
    return max(0, (totalEntries - 1) / (visibleRows * 2));
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
    if (!display || !node || !node->is_favorite || !isTFTColoringEnabled() || !nodeName) {
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
    bool isMuted = (node->bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) != 0;

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
    if (node->is_favorite) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (node->is_ignored || isMuted) {
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
    bool isMuted = (node->bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) != 0;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (node->is_favorite) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (node->is_ignored || isMuted) {
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
        const int iconY = y + (FONT_HEIGHT_SMALL - hop_height) / 2;

        display->drawString(hopX, y, hopCount);
        display->drawXbm(hopX + hopCountWidth + gap, iconY, hop_width, hop_height, hop);
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
    bool isMuted = (node->bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) != 0;
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
    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (node->is_favorite) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (node->is_ignored || isMuted) {
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
    bool isMuted = (node->bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) != 0;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    UIRenderer::drawStringWithEmotes(display, nameX, y, nodeName, FONT_HEIGHT_SMALL, 1, false);
    if (node->is_favorite) {
        if (currentResolution == ScreenResolution::High) {
            drawScaledXBitmap16x16(x, y + 6, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint, display);
        } else {
            display->drawXbm(x, y + 5, smallbulletpoint_width, smallbulletpoint_height, smallbulletpoint);
        }
    }
    if (node->is_ignored || isMuted) {
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

    double nodeLat = node->position.latitude_i * 1e-7;
    double nodeLon = node->position.longitude_i * 1e-7;
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

void drawNodeListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *title,
                        EntryRenderer renderer, NodeExtrasRenderer extras, float headingRadian, double lat, double lon)
{
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
        if (locationScreen && !n->has_position)
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

#if defined(M5STACK_UNITC6L)
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

#if defined(M5STACK_UNITC6L)
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
#ifdef USE_EINK
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
    auto ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!ourNode || !nodeDB->hasValidPosition(ourNode)) {
        drawNodeListScreen(display, state, x, y, "Bearings", drawEntryCompass, drawCompassUnknown, headingRadian, 0.0, 0.0);
        return;
    }

    double lat = DegD(ourNode->position.latitude_i);
    double lon = DegD(ourNode->position.longitude_i);

#if defined(M5STACK_UNITC6L)
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
