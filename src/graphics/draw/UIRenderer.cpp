#include "configuration.h"
#if HAS_SCREEN
#include "CompassRenderer.h"
#include "GPSStatus.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "NodeListRenderer.h"
#if !MESHTASTIC_EXCLUDE_STATUS
#include "modules/StatusMessageModule.h"
#endif
#include "UIRenderer.h"
#include "airtime.h"
#include "gps/GeoCoord.h"
#include "graphics/EmoteRenderer.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/TimeFormatters.h"
#include "graphics/images.h"
#include "main.h"
#include "target_specific.h"
#include <OLEDDisplay.h>
#include <RTC.h>
#include <cstring>

// External variables
extern graphics::Screen *screen;
#if defined(M5STACK_UNITC6L)
static uint32_t lastSwitchTime = 0;
#endif
namespace graphics
{
NodeNum UIRenderer::currentFavoriteNodeNum = 0;
std::vector<meshtastic_NodeInfoLite *> graphics::UIRenderer::favoritedNodes;
static bool gBootSplashBoldPass = false;

static inline void drawSatelliteIcon(OLEDDisplay *display, int16_t x, int16_t y)
{
    int yOffset = (currentResolution == ScreenResolution::High) ? -5 : 1;
    if (currentResolution == ScreenResolution::High) {
        NodeListRenderer::drawScaledXBitmap16x16(x, y + yOffset, imgSatellite_width, imgSatellite_height, imgSatellite, display);
    } else {
        display->drawXbm(x + 1, y + yOffset, imgSatellite_width, imgSatellite_height, imgSatellite);
    }
}

struct StandardCompassNeedlePoints {
    int16_t northTipX;
    int16_t northTipY;
    int16_t northLeftX;
    int16_t northLeftY;
    int16_t northRightX;
    int16_t northRightY;
    int16_t southTipX;
    int16_t southTipY;
    int16_t southLeftX;
    int16_t southLeftY;
    int16_t southRightX;
    int16_t southRightY;
};

static inline void swapPoint(int16_t &ax, int16_t &ay, int16_t &bx, int16_t &by)
{
    const int16_t tx = ax;
    const int16_t ty = ay;
    ax = bx;
    ay = by;
    bx = tx;
    by = ty;
}

static inline void transformNeedlePoint(float localX, float localY, float sinHeading, float cosHeading, float scale,
                                        int16_t centerX, int16_t centerY, int16_t &outX, int16_t &outY)
{
    const float x = ((localX * cosHeading) - (localY * sinHeading)) * scale + centerX;
    const float y = ((localX * sinHeading) + (localY * cosHeading)) * scale + centerY;
    outX = static_cast<int16_t>(x);
    outY = static_cast<int16_t>(y);
}

static float getCompassRingAngleOffset(float heading)
{
    return (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING) ? -heading : 0.0f;
}

static inline StandardCompassNeedlePoints computeStandardCompassNeedlePoints(int16_t compassX, int16_t compassY,
                                                                             uint16_t compassDiam, float headingRadian,
                                                                             float centerGapPx)
{
    // Standard-style symmetric needle with a narrow waist and a tiny center gap
    // between north/south halves to prevent seam bleed while rotating.
    const float scaledDiam = compassDiam * 0.76f;
    const float gapNormHalf = (centerGapPx * 0.5f) / scaledDiam;
    const float sinHeading = sinf(headingRadian);
    const float cosHeading = cosf(headingRadian);

    StandardCompassNeedlePoints points{};
    transformNeedlePoint(0.0f, -0.5f, sinHeading, cosHeading, scaledDiam, compassX, compassY, points.northTipX, points.northTipY);
    transformNeedlePoint(-0.09f, -gapNormHalf, sinHeading, cosHeading, scaledDiam, compassX, compassY, points.northLeftX,
                         points.northLeftY);
    transformNeedlePoint(0.09f, -gapNormHalf, sinHeading, cosHeading, scaledDiam, compassX, compassY, points.northRightX,
                         points.northRightY);
    transformNeedlePoint(0.0f, 0.5f, sinHeading, cosHeading, scaledDiam, compassX, compassY, points.southTipX, points.southTipY);
    transformNeedlePoint(-0.09f, gapNormHalf, sinHeading, cosHeading, scaledDiam, compassX, compassY, points.southLeftX,
                         points.southLeftY);
    transformNeedlePoint(0.09f, gapNormHalf, sinHeading, cosHeading, scaledDiam, compassX, compassY, points.southRightX,
                         points.southRightY);
    return points;
}

static inline void drawCompassNorthOnlyLabel(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius,
                                             float heading)
{
    int16_t labelRadius = compassRadius;
    // CompassRenderer::drawCompassNorth() expands radius on high-res by +4.
    // Compensate so label placement stays aligned with the current UI layout.
    if (currentResolution == ScreenResolution::High && labelRadius > 4) {
        labelRadius -= 4;
    }
    graphics::CompassRenderer::drawCompassNorth(display, compassX, compassY, heading, labelRadius);
}

static inline void drawMonoCompass(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius, float heading)
{
    const StandardCompassNeedlePoints points =
        computeStandardCompassNeedlePoints(compassX, compassY, static_cast<uint16_t>(compassRadius * 2), -heading, 0.0f);

#ifdef USE_EINK
    display->setColor(WHITE);
    display->drawTriangle(points.northTipX, points.northTipY, points.northLeftX, points.northLeftY, points.northRightX,
                          points.northRightY);
    display->drawTriangle(points.southTipX, points.southTipY, points.southLeftX, points.southLeftY, points.southRightX,
                          points.southRightY);
#else
    // OLED variant: same needle geometry as TFT, but monochrome contrast.
    display->setColor(WHITE);
    display->fillTriangle(points.northTipX, points.northTipY, points.northLeftX, points.northLeftY, points.northRightX,
                          points.northRightY);
    display->setColor(BLACK);
    display->fillTriangle(points.southTipX, points.southTipY, points.southLeftX, points.southLeftY, points.southRightX,
                          points.southRightY);
    // Keep a white outline so the black half remains visible on dark backgrounds.
    display->setColor(WHITE);
    display->drawTriangle(points.southTipX, points.southTipY, points.southLeftX, points.southLeftY, points.southRightX,
                          points.southRightY);
#endif

    display->drawCircle(compassX, compassY, compassRadius);
    drawCompassNorthOnlyLabel(display, compassX, compassY, compassRadius, heading);
}

#if GRAPHICS_TFT_COLORING_ENABLED
struct NeedleColorBand {
    int16_t xMin;
    int16_t xMax;
    int16_t yMin;
    int16_t yMax;
    bool used;
};

static constexpr int kNeedleBandCount = 6;

static inline void registerNeedleSpan(NeedleColorBand (&bands)[kNeedleBandCount], int16_t bandTop, int16_t bandHeight, int16_t y,
                                      int16_t a, int16_t b)
{
    if (a > b) {
        const int16_t t = a;
        a = b;
        b = t;
    }

    int band = (static_cast<int32_t>(y - bandTop) * kNeedleBandCount) / bandHeight;
    if (band < 0) {
        band = 0;
    } else if (band >= kNeedleBandCount) {
        band = kNeedleBandCount - 1;
    }

    NeedleColorBand &region = bands[band];
    if (!region.used) {
        region.used = true;
        region.xMin = a;
        region.xMax = b;
        region.yMin = y;
        region.yMax = y;
        return;
    }
    if (a < region.xMin)
        region.xMin = a;
    if (b > region.xMax)
        region.xMax = b;
    if (y < region.yMin)
        region.yMin = y;
    if (y > region.yMax)
        region.yMax = y;
}

static void drawNeedleHalfAndRegisterBands(OLEDDisplay *display, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2,
                                           int16_t y2, uint16_t onColor, uint16_t offColor)
{
    // Important for maintainers:
    // The compass needle rotates continuously, so color-region registration must
    // track triangle shape (or a close approximation), not only one AABB.
    // Coarse rectangles can leak south color into north at diagonal angles.
    // Keep this banded approach unless a replacement preserves per-angle coverage.
    // Performance note: draw the triangle once via fillTriangle(), then build
    // band regions in software for accurate color-role registration.
    display->fillTriangle(x0, y0, x1, y1, x2, y2);

    if (y0 > y1)
        swapPoint(x0, y0, x1, y1);
    if (y1 > y2)
        swapPoint(x1, y1, x2, y2);
    if (y0 > y1)
        swapPoint(x0, y0, x1, y1);

    NeedleColorBand bands[kNeedleBandCount] = {};

    const int16_t bandTop = y0;
    const int16_t bandBottom = y2;
    const int16_t bandHeight = (bandBottom >= bandTop) ? static_cast<int16_t>(bandBottom - bandTop + 1) : 1;

    const int32_t dx01 = x1 - x0;
    const int32_t dy01 = y1 - y0;
    const int32_t dx02 = x2 - x0;
    const int32_t dy02 = y2 - y0;
    const int32_t dx12 = x2 - x1;
    const int32_t dy12 = y2 - y1;

    int32_t sa = 0;
    int32_t sb = 0;
    int16_t y = y0;

    const int16_t last = (y1 == y2) ? y1 : static_cast<int16_t>(y1 - 1);
    for (; y <= last; y++) {
        const int16_t a = static_cast<int16_t>(x0 + ((dy01 != 0) ? (sa / dy01) : 0));
        const int16_t b = static_cast<int16_t>(x0 + ((dy02 != 0) ? (sb / dy02) : 0));
        sa += dx01;
        sb += dx02;
        registerNeedleSpan(bands, bandTop, bandHeight, y, a, b);
    }

    sa = dx12 * static_cast<int32_t>(y - y1);
    sb = dx02 * static_cast<int32_t>(y - y0);
    for (; y <= y2; y++) {
        const int16_t a = static_cast<int16_t>(x1 + ((dy12 != 0) ? (sa / dy12) : 0));
        const int16_t b = static_cast<int16_t>(x0 + ((dy02 != 0) ? (sb / dy02) : 0));
        sa += dx12;
        sb += dx02;
        registerNeedleSpan(bands, bandTop, bandHeight, y, a, b);
    }

    for (int i = 0; i < kNeedleBandCount; i++) {
        if (!bands[i].used)
            continue;
        registerTFTColorRegionDirect(bands[i].xMin, bands[i].yMin, bands[i].xMax - bands[i].xMin + 1,
                                     bands[i].yMax - bands[i].yMin + 1, onColor, offColor);
    }
}

static inline void drawCompassCardinalLabel(OLEDDisplay *display, int16_t x, int16_t y, const char *label, int16_t textWidth)
{
    const int16_t labelTop = y - (FONT_HEIGHT_SMALL / 2);
    const int16_t padX = 1;
    const int16_t padY = 1;

    // Clear any ring/tick pixels behind the label so letters remain clean.
    display->setColor(BLACK);
    display->fillRect(x - (textWidth / 2) - padX, labelTop - padY, textWidth + (padX * 2), FONT_HEIGHT_SMALL + (padY * 2));

    display->setColor(WHITE);
    display->drawString(x, labelTop, label);
}

static inline void drawCompassCardinalLabels(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius,
                                             float heading)
{
    const float northAngle = getCompassRingAngleOffset(heading);
    const float radius = compassRadius - 1.0f;
    const float sinNorth = sinf(northAngle);
    const float cosNorth = cosf(northAngle);

    const int16_t nX = compassX + static_cast<int16_t>(radius * sinNorth);
    const int16_t nY = compassY - static_cast<int16_t>(radius * cosNorth);
    const int16_t eX = compassX + static_cast<int16_t>(radius * cosNorth);
    const int16_t eY = compassY + static_cast<int16_t>(radius * sinNorth);
    const int16_t sX = compassX - static_cast<int16_t>(radius * sinNorth);
    const int16_t sY = compassY + static_cast<int16_t>(radius * cosNorth);
    const int16_t wX = compassX - static_cast<int16_t>(radius * cosNorth);
    const int16_t wY = compassY - static_cast<int16_t>(radius * sinNorth);

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const int16_t labelWidth = static_cast<int16_t>(display->getStringWidth("N"));
    drawCompassCardinalLabel(display, nX, nY, "N", labelWidth);
    drawCompassCardinalLabel(display, eX, eY, "E", labelWidth);
    drawCompassCardinalLabel(display, sX, sY, "S", labelWidth);
    drawCompassCardinalLabel(display, wX, wY, "W", labelWidth);
}

static inline void drawCompassDegreeMarkers(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius,
                                            float heading)
{
    const float baseAngle = getCompassRingAngleOffset(heading);

    constexpr int16_t majorLen = 5;
    constexpr int16_t minorLen = 3;

    display->setColor(WHITE);
    constexpr float kStepAngle = 15.0f * DEG_TO_RAD;
    const float sinStep = sinf(kStepAngle);
    const float cosStep = cosf(kStepAngle);
    float sinAngle = sinf(baseAngle);
    float cosAngle = cosf(baseAngle);
    bool isMajor = true;
    for (int tick = 0; tick < 24; tick++) {
        const int16_t tickLen = isMajor ? majorLen : minorLen;

        const int16_t xOuter = compassX + static_cast<int16_t>((compassRadius - 1) * sinAngle);
        const int16_t yOuter = compassY - static_cast<int16_t>((compassRadius - 1) * cosAngle);
        const int16_t xInner = compassX + static_cast<int16_t>((compassRadius - tickLen) * sinAngle);
        const int16_t yInner = compassY - static_cast<int16_t>((compassRadius - tickLen) * cosAngle);
        display->drawLine(xInner, yInner, xOuter, yOuter);

        // Rotate [sin, cos] by a fixed step instead of recomputing trig 24x/frame.
        const float nextSin = (sinAngle * cosStep) + (cosAngle * sinStep);
        const float nextCos = (cosAngle * cosStep) - (sinAngle * sinStep);
        sinAngle = nextSin;
        cosAngle = nextCos;
        isMajor = !isMajor;
    }
}

static inline void drawStandardCompassNeedle(OLEDDisplay *display, int16_t compassX, int16_t compassY, uint16_t compassDiam,
                                             float headingRadian, uint16_t needleOffColor)
{
    const StandardCompassNeedlePoints points =
        computeStandardCompassNeedlePoints(compassX, compassY, compassDiam, headingRadian, 9.0f);

    display->setColor(WHITE);
#ifdef USE_EINK
    display->drawTriangle(points.northTipX, points.northTipY, points.northLeftX, points.northLeftY, points.northRightX,
                          points.northRightY);
    display->drawTriangle(points.southTipX, points.southTipY, points.southLeftX, points.southLeftY, points.southRightX,
                          points.southRightY);
#else
    // NOTE: do not collapse these to one region per half during "flash
    // optimization". The needle spins, and coarse rectangles will bleed color
    // across halves at diagonal angles.
    drawNeedleHalfAndRegisterBands(display, points.northTipX, points.northTipY, points.northLeftX, points.northLeftY,
                                   points.northRightX, points.northRightY, TFTPalette::Red, needleOffColor);
    drawNeedleHalfAndRegisterBands(display, points.southTipX, points.southTipY, points.southLeftX, points.southLeftY,
                                   points.southRightX, points.southRightY, TFTPalette::Blue, needleOffColor);
#endif
}

static inline void drawTftCompass(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius, float heading)
{
    // Compass colors should follow whatever background role is already active at this location.
    const uint16_t compassBgColor = resolveTFTOffColorAt(compassX, compassY, getThemeBodyBg());
    const uint16_t compassGlyphColor = TFTPalette::pickReadableMonoFg(compassBgColor);
    const int16_t pad = 2;
    const int16_t labelPadX = static_cast<int16_t>(display->getStringWidth("W") / 2) + 2;
    const int16_t labelPadY = static_cast<int16_t>(FONT_HEIGHT_SMALL / 2) + 2;
    const int16_t boxX = compassX - compassRadius - pad - labelPadX;
    const int16_t boxY = compassY - compassRadius - pad - labelPadY;
    const int16_t boxW = (compassRadius * 2) + (pad * 2) + 1 + (labelPadX * 2);
    const int16_t boxH = (compassRadius * 2) + (pad * 2) + 1 + (labelPadY * 2);
    // Never let compass-local tint regions override the header role regions.
    const int16_t bodyTop = static_cast<int16_t>(getTextPositions(display)[1]);
    int16_t clippedY = boxY;
    int16_t clippedH = boxH;
    if (clippedY < bodyTop) {
        clippedH = static_cast<int16_t>(clippedH - (bodyTop - clippedY));
        clippedY = bodyTop;
    }
    if (clippedH > 0) {
        registerTFTColorRegionDirect(boxX, clippedY, boxW, clippedH, compassGlyphColor, compassBgColor);
    }

    drawStandardCompassNeedle(display, compassX, compassY, static_cast<uint16_t>(compassRadius * 2), -heading, compassBgColor);
    display->drawCircle(compassX, compassY, compassRadius);
    drawCompassDegreeMarkers(display, compassX, compassY, compassRadius, heading);
    drawCompassCardinalLabels(display, compassX, compassY, compassRadius, heading);
}
#endif // GRAPHICS_TFT_COLORING_ENABLED

static void drawCompassStatusText(OLEDDisplay *display, int16_t compassX, int16_t compassY, const char *statusLine1,
                                  const char *statusLine2)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(compassX, compassY - FONT_HEIGHT_SMALL, statusLine1);
    display->drawString(compassX, compassY, statusLine2);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

static void drawBearingCompassOrStatus(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius,
                                       bool showCompass, float myHeading, float bearing, const char *statusLine1,
                                       const char *statusLine2)
{
    // Shared "favorite node" compass renderer: draw ring, then either heading data or fallback status text.
    display->drawCircle(compassX, compassY, compassRadius);
    if (showCompass) {
        CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading, compassRadius);
        CompassRenderer::drawNodeHeading(display, compassX, compassY, compassRadius * 2, bearing);
    } else {
        drawCompassStatusText(display, compassX, compassY, statusLine1, statusLine2);
    }
}

static void drawDetailedCompassOrStatus(OLEDDisplay *display, int16_t compassX, int16_t compassY, int16_t compassRadius,
                                        bool validHeading, float heading, const char *statusLine1, const char *statusLine2)
{
    // Shared "position screen" compass renderer: use mono/TFT path only when heading is valid.
    if (validHeading) {
#if GRAPHICS_TFT_COLORING_ENABLED
        drawTftCompass(display, compassX, compassY, compassRadius, heading);
#else
        drawMonoCompass(display, compassX, compassY, compassRadius, heading);
#endif
    } else {
        display->drawCircle(compassX, compassY, compassRadius);
        drawCompassStatusText(display, compassX, compassY, statusLine1, statusLine2);
    }
}

static bool computeLandscapeCompassPlacement(OLEDDisplay *display, int16_t xOffset, int16_t topY, int16_t *compassX,
                                             int16_t *compassY, int16_t *compassRadius)
{
    // Keep compass vertically centered in the body area while reserving footer/nav space.
    const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1);
    const int16_t usableHeight = bottomY - topY - 5;
    int16_t radius = usableHeight / 2;
    if (radius < 8) {
        radius = 8;
    }

    *compassRadius = radius;
    *compassX = xOffset + SCREEN_WIDTH - radius - 8;
    *compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;
    return true;
}

static bool computeBottomCompassPlacement(OLEDDisplay *display, int16_t xOffset, int16_t yBelowContent, int16_t bottomReserved,
                                          int16_t margin, int16_t *compassX, int16_t *compassY, int16_t *compassRadius)
{
    // Return false when content leaves no room for a readable compass.
    int availableHeight = SCREEN_HEIGHT - yBelowContent - bottomReserved - margin;
    if (availableHeight < FONT_HEIGHT_SMALL * 2) {
        return false;
    }

    int16_t radius = static_cast<int16_t>(availableHeight / 2);
    if (radius < 8) {
        radius = 8;
    }
    if (radius * 2 > SCREEN_WIDTH - 16) {
        radius = (SCREEN_WIDTH - 16) / 2;
    }

    *compassRadius = radius;
    *compassX = xOffset + (SCREEN_WIDTH / 2);
    *compassY = static_cast<int16_t>(yBelowContent + (availableHeight / 2));
    return true;
}

static void drawTruncatedStatusLine(OLEDDisplay *display, int16_t x, int16_t y, const std::string &statusText)
{
    // Fixed-buffer truncate helper replaces iterative std::string chopping to keep code size down.
    char rawStatus[96];
    snprintf(rawStatus, sizeof(rawStatus), " Status: %s", statusText.c_str());

    char clippedStatus[96];
    UIRenderer::truncateStringWithEmotes(display, rawStatus, clippedStatus, sizeof(clippedStatus), display->getWidth());
    display->drawString(x, y, clippedStatus);
}

static int computeChannelUtilizationFill(int percent, int maxFill)
{
    // Compact linear fill mapping for the utilization bar.
    if (percent <= 0 || maxFill <= 0) {
        return 0;
    }
    if (percent >= 100) {
        return maxFill;
    }
    return (maxFill * percent + 50) / 100;
}

void graphics::UIRenderer::rebuildFavoritedNodes()
{
    favoritedNodes.clear();
    size_t total = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < total; i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum())
            continue;
        if (n->is_favorite)
            favoritedNodes.push_back(n);
    }

    std::sort(favoritedNodes.begin(), favoritedNodes.end(),
              [](const meshtastic_NodeInfoLite *a, const meshtastic_NodeInfoLite *b) { return a->num < b->num; });
}

#if !MESHTASTIC_EXCLUDE_GPS
// GeoCoord object for coordinate conversions
extern GeoCoord geoCoord;

// Threshold values for the GPS lock accuracy bar display
extern uint32_t dopThresholds[5];

// Draw GPS status summary
void UIRenderer::drawGps(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    // Draw satellite image
    if (currentResolution == ScreenResolution::High) {
        NodeListRenderer::drawScaledXBitmap16x16(x, y - 2, imgSatellite_width, imgSatellite_height, imgSatellite, display);
    } else {
        display->drawXbm(x + 1, y + 1, imgSatellite_width, imgSatellite_height, imgSatellite);
    }
    char textString[10];

    if (config.position.fixed_position) {
        // GPS coordinates are currently fixed
        snprintf(textString, sizeof(textString), "Fixed");
    }
    if (!gps->getIsConnected()) {
        snprintf(textString, sizeof(textString), "No Lock");
    }
    if (!gps->getHasLock()) {
        // Draw "No sats" to the right of the icon with slightly more gap
        snprintf(textString, sizeof(textString), "No Sats");
    } else {
        snprintf(textString, sizeof(textString), "%u sats", gps->getNumSatellites());
    }
    if (currentResolution == ScreenResolution::High) {
        display->drawString(x + 18, y, textString);
    } else {
        display->drawString(x + 11, y, textString);
    }
}

// Draw status when GPS is disabled or not present
void UIRenderer::drawGpsPowerStatus(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
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

void UIRenderer::drawGpsAltitude(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
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
void UIRenderer::drawGpsCoordinates(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps,
                                    const char *mode)
{
    auto gpsFormat = uiconfig.gps_format;
    char displayLine[32];

    if (!gps->getIsConnected() && !config.position.fixed_position) {
        if (strcmp(mode, "line1") == 0) {
            strcpy(displayLine, "No GPS present");
            display->drawString(x, y, displayLine);
        }
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        if (strcmp(mode, "line1") == 0) {
            strcpy(displayLine, "No GPS Lock");
            display->drawString(x, y, displayLine);
        }
    } else {

        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));

        if (gpsFormat != meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS) {
            char coordinateLine_1[22];
            char coordinateLine_2[22];
            if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC) { // Decimal Degrees
                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "Lat: %f", geoCoord.getLatitude() * 1e-7);
                snprintf(coordinateLine_2, sizeof(coordinateLine_2), "Lon: %f", geoCoord.getLongitude() * 1e-7);
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM) { // Universal Transverse Mercator
                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%2i%1c %06u E", geoCoord.getUTMZone(),
                         geoCoord.getUTMBand(), geoCoord.getUTMEasting());
                snprintf(coordinateLine_2, sizeof(coordinateLine_2), "%07u N", geoCoord.getUTMNorthing());
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS) { // Military Grid Reference System
                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%2i%1c %1c%1c", geoCoord.getMGRSZone(),
                         geoCoord.getMGRSBand(), geoCoord.getMGRSEast100k(), geoCoord.getMGRSNorth100k());
                snprintf(coordinateLine_2, sizeof(coordinateLine_2), "%05u E %05u N", geoCoord.getMGRSEasting(),
                         geoCoord.getMGRSNorthing());
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC) { // Open Location Code
                geoCoord.getOLCCode(coordinateLine_1);
                coordinateLine_2[0] = '\0';
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR) { // Ordnance Survey Grid Reference
                if (geoCoord.getOSGRE100k() == 'I' || geoCoord.getOSGRN100k() == 'I') { // OSGR is only valid around the UK region
                    snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%s", "Out of Boundary");
                    coordinateLine_2[0] = '\0';
                } else {
                    snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%1c%1c", geoCoord.getOSGRE100k(),
                             geoCoord.getOSGRN100k());
                    snprintf(coordinateLine_2, sizeof(coordinateLine_2), "%05u E %05u N", geoCoord.getOSGREasting(),
                             geoCoord.getOSGRNorthing());
                }
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS) { // Maidenhead Locator System
                double lat = geoCoord.getLatitude() * 1e-7;
                double lon = geoCoord.getLongitude() * 1e-7;

                // Normalize
                if (lat > 90.0)
                    lat = 90.0;
                if (lat < -90.0)
                    lat = -90.0;
                while (lon < -180.0)
                    lon += 360.0;
                while (lon >= 180.0)
                    lon -= 360.0;

                double adjLon = lon + 180.0;
                double adjLat = lat + 90.0;

                char maiden[10]; // enough for 8-char + null

                // Field (2 letters)
                int lonField = int(adjLon / 20.0);
                int latField = int(adjLat / 10.0);
                adjLon -= lonField * 20.0;
                adjLat -= latField * 10.0;

                // Square (2 digits)
                int lonSquare = int(adjLon / 2.0);
                int latSquare = int(adjLat / 1.0);
                adjLon -= lonSquare * 2.0;
                adjLat -= latSquare * 1.0;

                // Subsquare (2 letters)
                double lonUnit = 2.0 / 24.0;
                double latUnit = 1.0 / 24.0;
                int lonSub = int(adjLon / lonUnit);
                int latSub = int(adjLat / latUnit);

                snprintf(maiden, sizeof(maiden), "%c%c%c%c%c%c", 'A' + lonField, 'A' + latField, '0' + lonSquare, '0' + latSquare,
                         'A' + lonSub, 'A' + latSub);

                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "MH: %s", maiden);
                coordinateLine_2[0] = '\0'; // only need one line
            }

            if (strcmp(mode, "line1") == 0) {
                display->drawString(x, y, coordinateLine_1);
            } else if (strcmp(mode, "line2") == 0) {
                display->drawString(x, y, coordinateLine_2);
            } else if (strcmp(mode, "combined") == 0) {
                display->drawString(x, y, coordinateLine_1);
                if (coordinateLine_2[0] != '\0') {
                    display->drawString(x + display->getStringWidth(coordinateLine_1), y, coordinateLine_2);
                }
            }

        } else {
            char coordinateLine_1[22];
            char coordinateLine_2[22];
            snprintf(coordinateLine_1, sizeof(coordinateLine_1), "Lat: %2i° %2i' %2u\" %1c", geoCoord.getDMSLatDeg(),
                     geoCoord.getDMSLatMin(), geoCoord.getDMSLatSec(), geoCoord.getDMSLatCP());
            snprintf(coordinateLine_2, sizeof(coordinateLine_2), "Lon: %3i° %2i' %2u\" %1c", geoCoord.getDMSLonDeg(),
                     geoCoord.getDMSLonMin(), geoCoord.getDMSLonSec(), geoCoord.getDMSLonCP());
            if (strcmp(mode, "line1") == 0) {
                display->drawString(x, y, coordinateLine_1);
            } else if (strcmp(mode, "line2") == 0) {
                display->drawString(x, y, coordinateLine_2);
            } else { // both
                display->drawString(x, y, coordinateLine_1);
                display->drawString(x, y + 10, coordinateLine_2);
            }
        }
    }
}
#endif // !MESHTASTIC_EXCLUDE_GPS

// Draw nodes status
void UIRenderer::drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::NodeStatus *nodeStatus, int node_offset,
                           bool show_total, const char *additional_words)
{
    char usersString[20];
    int nodes_online = (nodeStatus->getNumOnline() > 0) ? nodeStatus->getNumOnline() + node_offset : 0;

    snprintf(usersString, sizeof(usersString), "%d %s", nodes_online, additional_words);

    if (show_total) {
        int nodes_total = (nodeStatus->getNumTotal() > 0) ? nodeStatus->getNumTotal() + node_offset : 0;
        snprintf(usersString, sizeof(usersString), "%d/%d %s", nodes_online, nodes_total, additional_words);
    }

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS) || defined(ST7796_CS) ||             \
     defined(HACKADAY_COMMUNICATOR) || defined(USE_ST7796)) &&                                                                   \
    !defined(DISPLAY_FORCE_SMALL_FONTS)

    if (currentResolution == ScreenResolution::High) {
        NodeListRenderer::drawScaledXBitmap16x16(x, y - 1, 8, 8, imgUser, display);
    } else {
        display->drawFastImage(x, y + 3, 8, 8, imgUser);
    }
#else
    if (currentResolution == ScreenResolution::High) {
        NodeListRenderer::drawScaledXBitmap16x16(x, y - 1, 8, 8, imgUser, display);
    } else {
        display->drawFastImage(x, y + 1, 8, 8, imgUser);
    }
#endif
    int string_offset = (currentResolution == ScreenResolution::High) ? 9 : 0;
    display->drawString(x + 10 + string_offset, y - 2, usersString);
}

// **********************
// * Favorite Node Info *
// **********************
// cppcheck-suppress constParameterPointer; signature must match FrameCallback typedef from OLEDDisplayUi library
void UIRenderer::drawFavoriteNode(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
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
#if defined(M5STACK_UNITC6L)
    uint32_t now = millis();
    if (now - lastSwitchTime >= 10000) // 10000 ms = 10 秒
    {
        display->display();
        lastSwitchTime = now;
    }
#endif
    currentFavoriteNodeNum = node->num;
    // === Create the shortName and title string ===
    const char *shortName = (node->has_user && node->user.short_name[0]) ? node->user.short_name : "Node";
    char titlestr[40];
    snprintf(titlestr, sizeof(titlestr), "*%s*", shortName);

    // === Draw battery/time/mail header (common across screens) ===
    graphics::drawCommonHeader(display, x, y, titlestr, false, false, false, true, TFTPalette::Yellow);

    // ===== DYNAMIC ROW STACKING WITH YOUR MACROS =====
    // 1. Each potential info row has a macro-defined Y position (not regular increments!).
    // 2. Each row is only shown if it has valid data.
    // 3. Each row "moves up" if previous are empty, so there are never any blank rows.
    // 4. The first line is ALWAYS at your macro position; subsequent lines use the next available macro slot.

    // List of available macro Y positions in order, from top to bottom.
    int line = 1; // which slot to use next
    // === 1. Long Name (always try to show first) ===
    const char *username;
    if (currentResolution == ScreenResolution::UltraLow) {
        username = (node->has_user && node->user.long_name[0]) ? node->user.short_name : nullptr;
    } else {
        username = (node->has_user && node->user.long_name[0]) ? node->user.long_name : nullptr;
    }

    // Print node's long name (e.g. "Backpack Node")
    if (username) {
#if GRAPHICS_TFT_COLORING_ENABLED
        const int usernameWidth = UIRenderer::measureStringWithEmotes(display, username);
        setAndRegisterTFTColorRole(TFTColorRole::FavoriteNodeBGHighlight, TFTPalette::Yellow, TFTPalette::Black, x,
                                   getTextPositions(display)[line], usernameWidth, FONT_HEIGHT_SMALL);
#endif
        UIRenderer::drawStringWithEmotes(display, x, getTextPositions(display)[line++], username, FONT_HEIGHT_SMALL, 1, false);
    }

#if !MESHTASTIC_EXCLUDE_STATUS
    // === Optional: Last received StatusMessage line for this node ===
    // Display it directly under the username line (if we have one).
    if (statusMessageModule) {
        const auto &recent = statusMessageModule->getRecentReceived();
        const StatusMessageModule::RecentStatus *found = nullptr;

        // Search newest-to-oldest
        for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
            if (it->fromNodeId == node->num && !it->statusText.empty()) {
                found = &(*it);
                break;
            }
        }

        if (found) {
            drawTruncatedStatusLine(display, x, getTextPositions(display)[line++], found->statusText);
        }
    }
#endif

    // === 2. Signal/Hops line (if available) ===
    bool haveSignal = false;
    int bars = 0;
    const char *qualityLabel = nullptr;

    // Helper to get SNR limit based on modem preset
    auto getSnrLimit = [](meshtastic_Config_LoRaConfig_ModemPreset preset) -> float {
        switch (preset) {
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
            return -6.0f;
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
            return -5.5f;
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
            return -4.5f;
        default:
            return -6.0f;
        }
    };

    // Add extra spacing on the left if we have an API connection to account for the common footer icons
    const char *leftSideSpacing =
        graphics::isAPIConnected(service->api_state) ? (currentResolution == ScreenResolution::High ? "     " : "   ") : " ";
    const bool isZeroHop = node->has_hops_away && node->hops_away == 0;

    // Signal text/bars are only for direct (zero-hop) nodes with valid SNR.
    if (isZeroHop) {
        float snr = node->snr;
        if (snr > -100 && snr != 0) {
            float snrLimit = getSnrLimit(config.lora.modem_preset);
            // Determine signal quality label and bars using SNR-only grading.
            if (snr > snrLimit + 10) {
                qualityLabel = "Good";
                bars = 4;
            } else if (snr > snrLimit + 6) {
                qualityLabel = "Good";
                bars = 3;
            } else if (snr > snrLimit + 2) {
                qualityLabel = "Good";
                bars = 2;
            } else if (snr > snrLimit - 4) {
                qualityLabel = "Fair";
                bars = 1;
            } else {
                qualityLabel = "Bad";
                bars = 1;
            }

            haveSignal = true;
        }
    }

    const bool showHops = node->has_hops_away && node->hops_away > 0;

    if (haveSignal || showHops) {
        int yPos = getTextPositions(display)[line++];
        int curX = x + display->getStringWidth(leftSideSpacing);

        // Draw signal quality text for zero-hop nodes when present.
        if (haveSignal && qualityLabel) {
            char signalLabel[20];
            snprintf(signalLabel, sizeof(signalLabel), "Sig:%s", qualityLabel);
            display->drawString(curX, yPos, signalLabel);
            curX += display->getStringWidth(signalLabel) + 4;
        }

        // Draw signal bars (skip on UltraLow, text only)
        if (currentResolution != ScreenResolution::UltraLow && haveSignal && bars > 0) {
            const int kMaxBars = 4;
            if (bars < 1)
                bars = 1;
            if (bars > kMaxBars)
                bars = kMaxBars;

            int barX = curX;

            const bool hi = (currentResolution == ScreenResolution::High);
            int barWidth = hi ? 2 : 1;
            int barGap = hi ? 2 : 1;
            int maxBarHeight = FONT_HEIGHT_SMALL - 7;
            if (!hi)
                maxBarHeight -= 1;
            int barY = yPos + (FONT_HEIGHT_SMALL - maxBarHeight) / 2;
            int totalBarsWidth = (kMaxBars * barWidth) + ((kMaxBars - 1) * barGap);

            uint16_t signalBarsColor = TFTPalette::Good;
            if (qualityLabel && strcmp(qualityLabel, "Fair") == 0) {
                signalBarsColor = TFTPalette::Medium;
            } else if (qualityLabel && strcmp(qualityLabel, "Bad") == 0) {
                signalBarsColor = TFTPalette::Bad;
            }
            setAndRegisterTFTColorRole(TFTColorRole::SignalBars, signalBarsColor, TFTPalette::Black, barX, barY, totalBarsWidth,
                                       maxBarHeight);

            for (int bi = 0; bi < kMaxBars; bi++) {
                int barHeight = maxBarHeight * (bi + 1) / kMaxBars;
                if (barHeight < 2)
                    barHeight = 2;

                int bx = barX + bi * (barWidth + barGap);
                int by = barY + maxBarHeight - barHeight;

                if (bi < bars) {
                    display->fillRect(bx, by, barWidth, barHeight);
                } else {
                    int baseY = barY + maxBarHeight - 1;
                    display->drawHorizontalLine(bx, baseY, barWidth);
                }
            }

            curX += totalBarsWidth + 2;
        }

        // Draw hops for non-zero-hop nodes as: number + hop icon.
        // This path is mutually exclusive with the zero-hop signal-bars path above.
        if (showHops) {
            display->drawString(curX, yPos, "Hop:");
            curX += display->getStringWidth("Hop:") + 2;

            char hopCount[6];
            snprintf(hopCount, sizeof(hopCount), "%d", node->hops_away);
            display->drawString(curX, yPos, hopCount);
            curX += display->getStringWidth(hopCount) + 2;

            const int iconY = yPos + (FONT_HEIGHT_SMALL - hop_height) / 2;
            display->drawXbm(curX, iconY, hop_width, hop_height, hop);
            curX += hop_width + 1;
        }
    }

    // === 3. Heard (last seen, skip if node never seen) ===
    char seenStr[20] = "";
    uint32_t seconds = sinceLastSeen(node);
    if (seconds != 0 && seconds != UINT32_MAX) {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        // Format as "Heard:Xm ago", "Heard:Xh ago", or "Heard:Xd ago"
        snprintf(seenStr, sizeof(seenStr), (days > 365 ? " Heard:?" : "%sHeard:%d%c ago"), leftSideSpacing,
                 (days    ? days
                  : hours ? hours
                          : minutes),
                 (days    ? 'd'
                  : hours ? 'h'
                          : 'm'));
    }
    if (seenStr[0]) {
        display->drawString(x, getTextPositions(display)[line++], seenStr);
    }
#if !defined(M5STACK_UNITC6L)
    // === 4. Uptime (only show if metric is present) ===
    char uptimeStr[32] = "";
    if (node->has_device_metrics && node->device_metrics.has_uptime_seconds) {
        char upPrefix[12]; // enough for leftSideSpacing + "Up:"
        snprintf(upPrefix, sizeof(upPrefix), "%sUp:", leftSideSpacing);
        getUptimeStr(node->device_metrics.uptime_seconds * 1000, upPrefix, uptimeStr, sizeof(uptimeStr));
    }
    if (uptimeStr[0]) {
        display->drawString(x, getTextPositions(display)[line++], uptimeStr);
    }

    // === 5. Distance (only if both nodes have GPS position) ===
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    char distStr[24] = ""; // Make buffer big enough for any string
    bool haveDistance = false;

    if (nodeDB->hasValidPosition(ourNode) && nodeDB->hasValidPosition(node)) {
        // Use shared meter conversion, then format display units with lightweight integer rounding.
        const float distanceMeters =
            GeoCoord::latLongToMeter(DegD(node->position.latitude_i), DegD(node->position.longitude_i),
                                     DegD(ourNode->position.latitude_i), DegD(ourNode->position.longitude_i));
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            const int feet = static_cast<int>((distanceMeters * METERS_TO_FEET) + 0.5f);
            if (feet > 0 && feet < 1000) {
                snprintf(distStr, sizeof(distStr), "%sDistance:%dft", leftSideSpacing, feet);
                haveDistance = true;
            } else {
                const int miles = (feet + 2640) / 5280; // rounded to nearest mile
                if (miles > 0 && miles < 1000) {
                    snprintf(distStr, sizeof(distStr), "%sDistance:%dmi", leftSideSpacing, miles);
                    haveDistance = true;
                }
            }
        } else {
            const int meters = static_cast<int>(distanceMeters + 0.5f);
            if (meters > 0 && meters < 1000) {
                snprintf(distStr, sizeof(distStr), "%sDistance:%dm", leftSideSpacing, meters);
                haveDistance = true;
            } else {
                const int km = (meters + 500) / 1000; // rounded to nearest km
                if (km > 0 && km < 1000) {
                    snprintf(distStr, sizeof(distStr), "%sDistance:%dkm", leftSideSpacing, km);
                    haveDistance = true;
                }
            }
        }
    }
    if (haveDistance && distStr[0]) {
        display->drawString(x, getTextPositions(display)[line++], distStr);
    }

    // === 6. Battery after Distance line, otherwise next available line ===
    char batLine[32] = "";
    bool haveBatLine = false;

    if (node->has_device_metrics) {
        bool hasPct = node->device_metrics.has_battery_level;
        bool hasVolt = node->device_metrics.has_voltage && node->device_metrics.voltage > 0.001f;

        int pct = 0;
        float volt = 0.0f;

        if (hasPct) {
            pct = (int)node->device_metrics.battery_level;
        }

        if (hasVolt) {
            volt = node->device_metrics.voltage;
        }

        if (hasPct && pct > 0 && pct <= 100) {
            // Normal battery percentage
            if (hasVolt) {
                snprintf(batLine, sizeof(batLine), "%sBat:%d%% (%.2fV)", leftSideSpacing, pct, volt);
            } else {
                snprintf(batLine, sizeof(batLine), "%sBat:%d%%", leftSideSpacing, pct);
            }
            haveBatLine = true;
        } else if (hasPct && pct > 100) {
            // Plugged in
            if (hasVolt) {
                snprintf(batLine, sizeof(batLine), "%sPlugged In (%.2fV)", leftSideSpacing, volt);
            } else {
                snprintf(batLine, sizeof(batLine), "%sPlugged In", leftSideSpacing);
            }
            haveBatLine = true;
        } else if (!hasPct && hasVolt) {
            // Voltage only
            snprintf(batLine, sizeof(batLine), "%sBat:%.2fV", leftSideSpacing, volt);
            haveBatLine = true;
        }
    }

    const int maxTextLines = (currentResolution == ScreenResolution::High) ? 6 : 5;

    // Only draw battery if it fits within the allowed lines
    if (haveBatLine && line <= maxTextLines) {
        display->drawString(x, getTextPositions(display)[line++], batLine);
    }

    bool showCompass = false;
    float myHeading = 0.0f;
    float bearing = 0.0f;
    const bool hasOwnPositionFix = (ourNode && nodeDB->hasValidPosition(ourNode));
    const bool hasNodePositionFix = nodeDB->hasValidPosition(node);
    const char *statusLine1 = nullptr;
    const char *statusLine2 = nullptr;
    if (hasOwnPositionFix && hasNodePositionFix) {
        const auto &op = ourNode->position;
        showCompass = CompassRenderer::getHeadingRadians(DegD(op.latitude_i), DegD(op.longitude_i), myHeading);
        if (showCompass) {
            const auto &p = node->position;
            bearing = GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            bearing = CompassRenderer::adjustBearingForCompassMode(bearing, myHeading);
        } else {
            statusLine1 = "No";
            statusLine2 = "Heading";
        }
    } else if (!hasOwnPositionFix || !hasNodePositionFix) {
        statusLine1 = "No";
        statusLine2 = "Fix";
    }

    // --- Compass Rendering: landscape (wide) screens use the original side-aligned logic ---
    if (showCompass || statusLine1) {
        int16_t compassX = 0;
        int16_t compassY = 0;
        int16_t compassRadius = 0;
        if (SCREEN_WIDTH > SCREEN_HEIGHT) {
            const int16_t topY = getTextPositions(display)[1];
            computeLandscapeCompassPlacement(display, x, topY, &compassX, &compassY, &compassRadius);
        } else {
            const int yBelowContent = (line > 0 && line <= 5) ? (getTextPositions(display)[line - 1] + FONT_HEIGHT_SMALL + 2)
                                                              : getTextPositions(display)[1];
#if defined(USE_EINK)
            const int iconSize = (currentResolution == ScreenResolution::High) ? 16 : 8;
            const int navBarHeight = iconSize + 6;
#else
            const int navBarHeight = 0;
#endif
            if (!computeBottomCompassPlacement(display, x, yBelowContent, navBarHeight, 4, &compassX, &compassY,
                                               &compassRadius)) {
                return;
            }
        }
        drawBearingCompassOrStatus(display, compassX, compassY, compassRadius, showCompass, myHeading, bearing, statusLine1,
                                   statusLine2);
    }
#endif
    graphics::drawCommonFooter(display, x, y);
}

// ****************************
// * Device Focused Screen    *
// ****************************
void UIRenderer::drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());

    // === Header ===
    if (currentResolution == ScreenResolution::UltraLow) {
        graphics::drawCommonHeader(display, x, y, "Home");
    } else {
        graphics::drawCommonHeader(display, x, y, "");
    }

    // === Content below header ===

    // === First Row: Region / Channel Utilization and Uptime ===
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    // Display Region and Channel Utilization
    if (currentResolution == ScreenResolution::UltraLow) {
        drawNodes(display, x, getTextPositions(display)[line] + 2, nodeStatus, -1, false, "online");
    } else {
        drawNodes(display, x + 1, getTextPositions(display)[line] + 2, nodeStatus, -1, false, "online");
    }
    char uptimeStr[32] = "";
    if (currentResolution != ScreenResolution::UltraLow) {
        getUptimeStr(millis(), "Up: ", uptimeStr, sizeof(uptimeStr));
    }
    display->drawString(SCREEN_WIDTH - display->getStringWidth(uptimeStr), getTextPositions(display)[line++], uptimeStr);

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
        drawSatelliteIcon(display, x, getTextPositions(display)[line]);
        int xOffset = (currentResolution == ScreenResolution::High) ? 6 : 0;
        display->drawString(x + 11 + xOffset, getTextPositions(display)[line], displayLine);
    } else {
        UIRenderer::drawGps(display, 0, getTextPositions(display)[line], gpsStatus);
    }
#endif

#if defined(M5STACK_UNITC6L)
    line += 1;

    // === Node Identity ===
    int textWidth = 0;
    int nameX = 0;
    const char *shortName = owner.short_name ? owner.short_name : "";

    // === ShortName Centered ===
    textWidth = UIRenderer::measureStringWithEmotes(display, shortName);
    nameX = (SCREEN_WIDTH - textWidth) / 2;
    UIRenderer::drawStringWithEmotes(display, nameX, getTextPositions(display)[line++], shortName, FONT_HEIGHT_SMALL, 1, false);
#else
    if (powerStatus->getHasBattery()) {
        char batStr[20];
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;
        snprintf(batStr, sizeof(batStr), "%01d.%02dV", batV, batCv);
        display->drawString(x + SCREEN_WIDTH - display->getStringWidth(batStr), getTextPositions(display)[line++], batStr);
    } else {
        display->drawString(x + SCREEN_WIDTH - display->getStringWidth("USB"), getTextPositions(display)[line++], "USB");
    }

    config.display.heading_bold = origBold;

    // === Third Row: Channel Utilization Bluetooth Off (Only If Actually Off) ===
    const char *chUtil = "ChUtil:";
    char chUtilPercentage[10];
    int chutil_percent = static_cast<int>(airTime->channelUtilizationPercent() + 0.5f);
    snprintf(chUtilPercentage, sizeof(chUtilPercentage), "%d%%", chutil_percent);

    int chUtil_x = (currentResolution == ScreenResolution::High) ? display->getStringWidth(chUtil) + 10
                                                                 : display->getStringWidth(chUtil) + 5;
    int chUtil_y = getTextPositions(display)[line] + 3;

    int chutil_bar_width = (currentResolution == ScreenResolution::High) ? 100 : 50;
    int chutil_bar_max_fill = chutil_bar_width - 2; // Account for border
    if (!config.bluetooth.enabled) {
#if defined(USE_EINK)
        chutil_bar_width = (currentResolution == ScreenResolution::High) ? 50 : 30;
#else
        chutil_bar_width = (currentResolution == ScreenResolution::High) ? 80 : 40;
#endif
    }
    int chutil_bar_height = (currentResolution == ScreenResolution::High) ? 12 : 7;
    int extraoffset = (currentResolution == ScreenResolution::High) ? 6 : 3;
    if (!config.bluetooth.enabled) {
        extraoffset = (currentResolution == ScreenResolution::High) ? 6 : 1;
    }
    const int raw_chutil_percent = chutil_percent;

    // With BT disabled we pin this row left to make room for the extra "BT off" indicator.
    const int starting_position = config.bluetooth.enabled ? x : 0;

    display->drawString(starting_position, getTextPositions(display)[line], chUtil);

    // Force 61% or higher to show a full 100% bar, text would still show related percent.
    if (chutil_percent >= 61) {
        chutil_percent = 100;
    }

    int fillRight = computeChannelUtilizationFill(chutil_percent, chutil_bar_max_fill);

    // Draw outline
    display->drawRect(starting_position + chUtil_x, chUtil_y, chutil_bar_width, chutil_bar_height);

    // Fill progress
    if (fillRight > 0) {
#if GRAPHICS_TFT_COLORING_ENABLED
        uint16_t UtilizationFillColor = TFTPalette::Good;
        if (raw_chutil_percent >= 60) {
            UtilizationFillColor = TFTPalette::Bad;
        } else if (raw_chutil_percent >= 35) {
            UtilizationFillColor = TFTPalette::Medium;
        }
        setAndRegisterTFTColorRole(TFTColorRole::UtilizationFill, UtilizationFillColor, TFTPalette::Black,
                                   starting_position + chUtil_x + 1, chUtil_y + 1, fillRight, chutil_bar_height - 2);
#endif
        display->fillRect(starting_position + chUtil_x + 1, chUtil_y + 1, fillRight, chutil_bar_height - 2);
    }

    display->drawString(starting_position + chUtil_x + chutil_bar_width + extraoffset, getTextPositions(display)[line],
                        chUtilPercentage);

    if (!config.bluetooth.enabled) {
        display->drawString(SCREEN_WIDTH - display->getStringWidth("BT off"), getTextPositions(display)[line], "BT off");
    }

    line += 1;

    // === Fourth & Fifth Rows: Node Identity ===
    int textWidth = 0;
    int nameX = 0;
    int yOffset = (currentResolution == ScreenResolution::High) ? 0 : 5;
    const char *longName = (ourNode && ourNode->has_user && ourNode->user.long_name[0]) ? ourNode->user.long_name : "";
    const char *shortName = owner.short_name ? owner.short_name : "";
    char combinedName[96];
    if (longName[0] && shortName[0]) {
        snprintf(combinedName, sizeof(combinedName), "%s (%s)", longName, shortName);
    } else if (longName[0]) {
        strncpy(combinedName, longName, sizeof(combinedName) - 1);
        combinedName[sizeof(combinedName) - 1] = '\0';
    } else {
        strncpy(combinedName, shortName, sizeof(combinedName) - 1);
        combinedName[sizeof(combinedName) - 1] = '\0';
    }
    if (SCREEN_WIDTH - UIRenderer::measureStringWithEmotes(display, combinedName) > 10) {
        textWidth = UIRenderer::measureStringWithEmotes(display, combinedName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        UIRenderer::drawStringWithEmotes(display, nameX, getTextPositions(display)[line++] + yOffset, combinedName,
                                         FONT_HEIGHT_SMALL, 1, false);
    } else {
        // === LongName Centered ===
        textWidth = UIRenderer::measureStringWithEmotes(display, longName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        UIRenderer::drawStringWithEmotes(display, nameX, getTextPositions(display)[line++], longName, FONT_HEIGHT_SMALL, 1,
                                         false);

        // === ShortName Centered ===
        textWidth = UIRenderer::measureStringWithEmotes(display, shortName);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        UIRenderer::drawStringWithEmotes(display, nameX, getTextPositions(display)[line++], shortName, FONT_HEIGHT_SMALL, 1,
                                         false);
    }
#endif
    graphics::drawCommonFooter(display, x, y);
}

// Start Functions to write date/time to the screen
// Helper function to check if a year is a leap year
constexpr bool isLeapYear(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

// Array of days in each month (non-leap year)
const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Fills the buffer with a formatted date/time string and returns pixel width
int UIRenderer::formatDateTime(char *buf, size_t bufSize, uint32_t rtc_sec, OLEDDisplay *display, bool includeTime)
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
bool UIRenderer::haveGlyphs(const char *str)
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
void UIRenderer::drawDeepSleepFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

    // Next frame should use full-refresh, and block while running, else device will sleep before async callback
    EINK_ADD_FRAMEFLAG(display, COSMETIC);
    EINK_ADD_FRAMEFLAG(display, BLOCKING);

    LOG_DEBUG("Draw deep sleep screen");

    // Display displayStr on the screen
    graphics::UIRenderer::drawIconScreen("Sleeping", display, state, x, y);
}

/// Used on eink displays when screen updates are paused
void UIRenderer::drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    LOG_DEBUG("Draw screensaver overlay");

    EINK_ADD_FRAMEFLAG(display, COSMETIC); // Full refresh for screensaver

    // Config
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *pauseText = "Screen Paused";
    const char *idText = owner.short_name;
    const bool useId = (idText && idText[0]);
    constexpr uint8_t padding = 2;
    constexpr uint8_t dividerGap = 1;

    // Text widths
    const uint16_t idTextWidth = useId ? UIRenderer::measureStringWithEmotes(display, idText) : 0;
    const uint16_t pauseTextWidth = display->getStringWidth(pauseText, strlen(pauseText));
    const uint16_t boxWidth = padding + (useId ? idTextWidth + padding : 0) + pauseTextWidth + padding;
    const uint16_t boxHeight = FONT_HEIGHT_SMALL + (padding * 2);

    // Flush with bottom
    const int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    const int16_t boxTop = display->height() - boxHeight;
    const int16_t boxBottom = display->height() - 1;
    const int16_t idTextLeft = boxLeft + padding;
    const int16_t idTextTop = boxTop + padding;
    const int16_t pauseTextLeft = boxLeft + (useId ? idTextWidth + (padding * 2) : 0) + padding;
    const int16_t pauseTextTop = boxTop + padding;
    const int16_t dividerX = boxLeft + padding + idTextWidth + padding;
    const int16_t dividerTop = boxTop + dividerGap;
    const int16_t dividerBottom = boxBottom - dividerGap;

    // Draw: box
    display->setColor(EINK_WHITE);
    display->fillRect(boxLeft, boxTop, boxWidth, boxHeight);
    display->setColor(EINK_BLACK);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);

    // Draw: text
    if (useId)
        UIRenderer::drawStringWithEmotes(display, idTextLeft, idTextTop, idText, FONT_HEIGHT_SMALL, 1, false);
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
void UIRenderer::drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // draw centered icon left to right and centered above the one line of app text
#if defined(M5STACK_UNITC6L)
    display->drawXbm(x + (SCREEN_WIDTH - 50) / 2, y + (SCREEN_HEIGHT - 28) / 2, icon_width, icon_height, icon_bits);
    if (gBootSplashBoldPass) {
        display->drawXbm(x + (SCREEN_WIDTH - 50) / 2 + 1, y + (SCREEN_HEIGHT - 28) / 2, icon_width, icon_height, icon_bits);
    }
    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    // Draw region in upper left
    if (upperMsg) {
        int msgWidth = display->getStringWidth(upperMsg);
        int msgX = x + (SCREEN_WIDTH - msgWidth) / 2;
        int msgY = y;
        display->drawString(msgX, msgY, upperMsg);
        if (gBootSplashBoldPass) {
            display->drawString(msgX + 1, msgY, upperMsg);
        }
    }
    // Draw version and short name in bottom middle
    char footer[64];
    if (owner.short_name && owner.short_name[0]) {
        snprintf(footer, sizeof(footer), "%s   %s", xstr(APP_VERSION_SHORT), owner.short_name);
    } else {
        snprintf(footer, sizeof(footer), "%s", xstr(APP_VERSION_SHORT));
    }
    int footerW = UIRenderer::measureStringWithEmotes(display, footer);
    int footerX = x + ((SCREEN_WIDTH - footerW) / 2);
    UIRenderer::drawStringWithEmotes(display, footerX, y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, footer, FONT_HEIGHT_SMALL, 1,
                                     false);
    if (gBootSplashBoldPass) {
        UIRenderer::drawStringWithEmotes(display, footerX + 1, y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, footer, FONT_HEIGHT_SMALL,
                                         1, false);
    }
    screen->forceDisplay();

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Restore left align, just to be kind to any other unsuspecting code
#else
    display->drawXbm(x + (SCREEN_WIDTH - icon_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 2,
                     icon_width, icon_height, icon_bits);

    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = "meshtastic.org";
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - 5, title);
    if (gBootSplashBoldPass) {
        display->drawString(x + getStringCenteredX(title) + 1, y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - 5, title);
    }
    display->setFont(FONT_SMALL);
    // Draw region in upper left
    if (upperMsg) {
        display->drawString(x + 5, y + 5, upperMsg);
        if (gBootSplashBoldPass) {
            display->drawString(x + 6, y + 5, upperMsg);
        }
    }

    // Draw version and short name in upper right
    const char *version = xstr(APP_VERSION_SHORT);
    int versionX = x + SCREEN_WIDTH - display->getStringWidth(version) - 5;
    display->drawString(versionX, y + 5, version);
    if (gBootSplashBoldPass) {
        display->drawString(versionX + 1, y + 5, version);
    }
    if (owner.short_name && owner.short_name[0]) {
        const char *shortName = owner.short_name;
        int shortNameW = UIRenderer::measureStringWithEmotes(display, shortName);
        int shortNameX = x + SCREEN_WIDTH - shortNameW - 5;
        UIRenderer::drawStringWithEmotes(display, shortNameX, y + 5 + FONT_HEIGHT_SMALL, shortName, FONT_HEIGHT_SMALL, 1, false);
        if (gBootSplashBoldPass) {
            UIRenderer::drawStringWithEmotes(display, shortNameX + 1, y + 5 + FONT_HEIGHT_SMALL, shortName, FONT_HEIGHT_SMALL, 1,
                                             false);
        }
    }
    screen->forceDisplay();

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Restore left align, just to be kind to any other unsuspecting code
#endif
}

void UIRenderer::drawBootIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if GRAPHICS_TFT_COLORING_ENABLED
    // Meshtastic brand green background with black foreground text/icon on TFT startup screen.
    static constexpr uint16_t kMeshtasticGreen = TFTPalette::rgb565(103, 234, 145);
    setAndRegisterTFTColorRole(TFTColorRole::BootSplash, TFTPalette::Black, kMeshtasticGreen, x, y, SCREEN_WIDTH, SCREEN_HEIGHT);
    gBootSplashBoldPass = true;
#endif
    drawIconScreen(upperMsg, display, state, x, y);
#if GRAPHICS_TFT_COLORING_ENABLED
    gBootSplashBoldPass = false;
#endif
}

// ****************************
// * My Position Screen       *
// ****************************
void UIRenderer::drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    // === Set Title
    const char *titleStr = "Position";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);
    const int *textPos = getTextPositions(display);

    // === First Row: My Location ===
#if HAS_GPS
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    const char *displayLine = ""; // Initialize to empty string by default

    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        if (config.position.fixed_position) {
            displayLine = "Fixed GPS";
        } else {
            displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        }
        drawSatelliteIcon(display, x, textPos[line]);
        int xOffset = (currentResolution == ScreenResolution::High) ? 6 : 0;
        display->drawString(x + 11 + xOffset, textPos[line++], displayLine);
    } else {
        // Onboard GPS
        UIRenderer::drawGps(display, 0, textPos[line++], gpsStatus);
    }

    config.display.heading_bold = origBold;

    // === Update GeoCoord ===
    geoCoord.updateCoords(int32_t(gpsStatus->getLatitude()), int32_t(gpsStatus->getLongitude()),
                          int32_t(gpsStatus->getAltitude()));

    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    const bool hasOwnPositionFix = (ourNode && nodeDB->hasValidPosition(ourNode));
    const bool hasLiveGpsFix =
        (gpsStatus && gpsStatus->getHasLock() && (gpsStatus->getLatitude() != 0 || gpsStatus->getLongitude() != 0));
    const bool hasSensorHeading = screen->hasHeading();
    float heading = 0.0f;
    bool validHeading = false;
    const char *statusLine1 = nullptr;
    const char *statusLine2 = nullptr;
    if (hasSensorHeading || hasLiveGpsFix || hasOwnPositionFix) {
        double headingLat = 0.0;
        double headingLon = 0.0;
        if (hasLiveGpsFix) {
            headingLat = DegD(gpsStatus->getLatitude());
            headingLon = DegD(gpsStatus->getLongitude());
        } else if (hasOwnPositionFix) {
            const auto &op = ourNode->position;
            headingLat = DegD(op.latitude_i);
            headingLon = DegD(op.longitude_i);
        }
        validHeading = CompassRenderer::getHeadingRadians(headingLat, headingLon, heading);
    }

    if (!validHeading) {
        if (hasSensorHeading || hasLiveGpsFix || hasOwnPositionFix) {
            statusLine1 = "No";
            statusLine2 = "Heading";
        } else {
            statusLine1 = "No";
            statusLine2 = "Fix";
        }
    }

    // If GPS is off, no need to display these parts
    if (strcmp(displayLine, "GPS off") != 0 && strcmp(displayLine, "No GPS") != 0) {
        // === Second Row: Last GPS Fix ===
        if (gpsStatus->getLastFixMillis() > 0) {
            uint32_t delta = millis() - gpsStatus->getLastFixMillis();
            char uptimeStr[32];
#if defined(USE_EINK)
            // E-Ink: skip seconds, show only days/hours/mins
            getUptimeStr(delta, "Last: ", uptimeStr, sizeof(uptimeStr), false);
#else
            // Non E-Ink: include seconds where useful
            getUptimeStr(delta, "Last: ", uptimeStr, sizeof(uptimeStr), true);
#endif

            display->drawString(0, textPos[line++], uptimeStr);
        } else {
            display->drawString(0, textPos[line++], "Last: ?");
        }

        // === Third Row: Line 1 GPS Info ===
        UIRenderer::drawGpsCoordinates(display, x, textPos[line++], gpsStatus, "line1");

        if (uiconfig.gps_format != meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC &&
            uiconfig.gps_format != meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS) {
            // === Fourth Row: Line 2 GPS Info ===
            UIRenderer::drawGpsCoordinates(display, x, textPos[line++], gpsStatus, "line2");
        }

        // === Final Row: Altitude ===
        char altitudeLine[32] = {0};
        int32_t alt = geoCoord.getAltitude();
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            snprintf(altitudeLine, sizeof(altitudeLine), "Alt: %.0fft", alt * METERS_TO_FEET);
        } else {
            snprintf(altitudeLine, sizeof(altitudeLine), "Alt: %.0im", alt);
        }
        display->drawString(x, textPos[line++], altitudeLine);
    }
#if !defined(M5STACK_UNITC6L)
    // === Draw Compass ===
    if (validHeading || statusLine1) {
        // --- Compass Rendering: landscape (wide) screens use original side-aligned logic ---
        if (SCREEN_WIDTH > SCREEN_HEIGHT) {
            const int16_t topY = textPos[1];
            const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1); // nav row height
            const int16_t usableHeight = bottomY - topY - 5;

            int16_t compassRadius = usableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;

            // Center vertically and nudge down slightly to keep "N" clear of header
            const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

            drawDetailedCompassOrStatus(display, compassX, compassY, compassRadius, validHeading, heading, statusLine1,
                                        statusLine2);
        } else {
            // Portrait or square: put compass at the bottom and centered, scaled to fit available space
            // For E-Ink screens, account for navigation bar at the bottom!
            const int yBelowContent = textPos[5] + FONT_HEIGHT_SMALL + 2;
#if defined(USE_EINK)
            const int margin = 4;
            int availableHeight = SCREEN_HEIGHT - yBelowContent - 24; // Leave extra space for nav bar on E-Ink
#else
            const int margin = 4;
            int availableHeight = SCREEN_HEIGHT - yBelowContent - margin;
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

            drawDetailedCompassOrStatus(display, compassX, compassY, compassRadius, validHeading, heading, statusLine1,
                                        statusLine2);
        }
    }
#endif
#endif // HAS_GPS
    graphics::drawCommonFooter(display, x, y);
}

#ifdef USERPREFS_OEM_TEXT

void UIRenderer::drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    static const uint8_t xbm[] = USERPREFS_OEM_IMAGE_DATA;
    if (currentResolution == ScreenResolution::High) {
        display->drawXbm(x + (SCREEN_WIDTH - USERPREFS_OEM_IMAGE_WIDTH) / 2,
                         y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - USERPREFS_OEM_IMAGE_HEIGHT) / 2 + 2, USERPREFS_OEM_IMAGE_WIDTH,
                         USERPREFS_OEM_IMAGE_HEIGHT, xbm);
    } else {

        display->drawXbm(x + (SCREEN_WIDTH - USERPREFS_OEM_IMAGE_WIDTH) / 2,
                         y + (SCREEN_HEIGHT - USERPREFS_OEM_IMAGE_HEIGHT) / 2 + 2, USERPREFS_OEM_IMAGE_WIDTH,
                         USERPREFS_OEM_IMAGE_HEIGHT, xbm);
    }

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
    if (currentResolution == ScreenResolution::High) {
        display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    }
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version and shortname in upper right
    const char *version = xstr(APP_VERSION_SHORT);
    int versionX = x + SCREEN_WIDTH - display->getStringWidth(version);
    display->drawString(versionX, y + 0, version);
    if (owner.short_name && owner.short_name[0]) {
        const char *shortName = owner.short_name;
        int shortNameW = UIRenderer::measureStringWithEmotes(display, shortName);
        int shortNameX = x + SCREEN_WIDTH - shortNameW;
        UIRenderer::drawStringWithEmotes(display, shortNameX, y + FONT_HEIGHT_SMALL, shortName, FONT_HEIGHT_SMALL, 1, false);
    }
    screen->forceDisplay();

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Restore left align, just to be kind to any other unsuspecting code
}

void UIRenderer::drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawOEMIconScreen(region, display, state, x, y);
}

#endif

// Navigation bar overlay implementation
static int16_t lastFrameIndex = -1;
static uint32_t lastFrameChangeTime = 0;
constexpr uint32_t ICON_DISPLAY_DURATION_MS = 2000;

// cppcheck-suppress constParameterPointer; signature must match OverlayCallback typedef from OLEDDisplayUi library
void UIRenderer::drawNavigationBar(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    uint8_t frameToHighlight = state->currentFrame;
    if (state->frameState == IN_TRANSITION && state->transitionFrameTarget < screen->indicatorIcons.size()) {
        frameToHighlight = state->transitionFrameTarget;
    }

    // Detect frame change and record time
    if (frameToHighlight != lastFrameIndex) {
        lastFrameIndex = frameToHighlight;
        lastFrameChangeTime = millis();
    }

    const int iconSize = (currentResolution == ScreenResolution::High) ? 16 : 8;
    const int spacing = (currentResolution == ScreenResolution::High) ? 8 : 4;
    const int bigOffset = (currentResolution == ScreenResolution::High) ? 1 : 0;

    const size_t totalIcons = screen->indicatorIcons.size();
    if (totalIcons == 0)
        return;

    const int navPadding = (currentResolution == ScreenResolution::High) ? 24 : 12; // padding per side

    int usableWidth = SCREEN_WIDTH - (navPadding * 2);
    if (usableWidth < iconSize)
        usableWidth = iconSize;

    const size_t iconsPerPage = usableWidth / (iconSize + spacing);
    const size_t currentPage = frameToHighlight / iconsPerPage;
    const size_t pageStart = currentPage * iconsPerPage;
    const size_t pageEnd = min(pageStart + iconsPerPage, totalIcons);

    const int totalWidth = (pageEnd - pageStart) * iconSize + (pageEnd - pageStart - 1) * spacing;
    const int xStart = (SCREEN_WIDTH - totalWidth) / 2;

    const bool navBarVisible = millis() - lastFrameChangeTime <= ICON_DISPLAY_DURATION_MS;
    const int y = navBarVisible ? (SCREEN_HEIGHT - iconSize - 1) : SCREEN_HEIGHT;

#if defined(USE_EINK)
    // Only show bar briefly after switching frames
    static uint32_t navBarLastShown = 0;
    static bool cosmeticRefreshDone = false;
    static bool navBarPrevVisible = false;

    if (navBarVisible && !navBarPrevVisible) {
        EINK_ADD_FRAMEFLAG(display, DEMAND_FAST); // Fast refresh when showing nav bar
        cosmeticRefreshDone = false;
        navBarLastShown = millis();
    }

    if (!navBarVisible && navBarPrevVisible) {
        EINK_ADD_FRAMEFLAG(display, DEMAND_FAST); // Fast refresh when hiding nav bar
        navBarLastShown = millis();               // Mark when it disappeared
    }

    if (!navBarVisible && navBarLastShown != 0 && !cosmeticRefreshDone) {
        if (millis() - navBarLastShown > 10000) {  // 10s after hidden
            EINK_ADD_FRAMEFLAG(display, COSMETIC); // One-time ghost cleanup
            cosmeticRefreshDone = true;
        }
    }

    navBarPrevVisible = navBarVisible;
#endif

    // Pre-calculate bounding rect
    const int rectX = xStart - 2 - bigOffset;
    const int rectY = y - 2;
    const int rectWidth = totalWidth + 4 + (bigOffset * 2);
    const int rectHeight = iconSize + 6;

    // Clear background and draw border
    display->setColor(BLACK);
#if GRAPHICS_TFT_COLORING_ENABLED
    // NavigationBar and NavigationArrow roles are fully defined in the theme table.
    // We must call setTFTColorRole() before registerTFTColorRegion() because
    // registerTFTColorRegion() snapshots colors from the roleColors[] working array,
    // and loadThemeDefaults() isn't guaranteed to have run since boot.
    const TFTThemeDef &theme = getActiveTheme();
    const auto &navBarRole = theme.roles[static_cast<size_t>(TFTColorRole::NavigationBar)];
    const auto &navArrowRole = theme.roles[static_cast<size_t>(TFTColorRole::NavigationArrow)];

    setAndRegisterTFTColorRole(TFTColorRole::NavigationBar, navBarRole.onColor, navBarRole.offColor, rectX, rectY, rectWidth,
                               rectHeight);
    setTFTColorRole(TFTColorRole::NavigationArrow, navArrowRole.onColor, navArrowRole.offColor);
    display->fillRect(rectX, rectY, rectWidth, rectHeight);
#else
    // Keep legacy OLED behavior untouched.
    display->fillRect(rectX + 1, rectY, rectWidth - 2, rectHeight - 2);
    display->setColor(WHITE);
    display->drawRect(rectX, rectY, rectWidth, rectHeight);
#endif

    // Icons are 1-bit glyphs and must be drawn with WHITE to set pixels.
    display->setColor(WHITE);

    // Icon drawing loop for the current page
    for (size_t i = pageStart; i < pageEnd; ++i) {
        const uint8_t *icon = screen->indicatorIcons[i];
        const int x = xStart + (i - pageStart) * (iconSize + spacing);
        const bool isActive = (i == static_cast<size_t>(frameToHighlight));

        if (isActive) {
#if GRAPHICS_TFT_COLORING_ENABLED
            // Active icon inverts on TFT: white chip with black glyph.
            // Keep the buffer visibly different too, so dirty-rect updates include this region.
            registerTFTColorRegion(TFTColorRole::NavigationBar, x - 1, y - 1, iconSize + 2, iconSize + 2);
            display->setColor(WHITE);
            display->fillRect(x - 1, y - 1, iconSize + 2, iconSize + 2);
            display->setColor(BLACK);
#else
            display->setColor(WHITE);
            display->fillRect(x - 2, y - 2, iconSize + 4, iconSize + 4);
            display->setColor(BLACK);
#endif
        }

        if (currentResolution == ScreenResolution::High) {
            NodeListRenderer::drawScaledXBitmap16x16(x, y, 8, 8, icon, display);
        } else {
            display->drawXbm(x, y, iconSize, iconSize, icon);
        }

        if (isActive) {
            display->setColor(WHITE);
        }
    }

    display->setColor(WHITE);

    const int offset = (currentResolution == ScreenResolution::High) ? 3 : 1;
    const int halfH = rectHeight / 2;
    const int top = rectY + (rectHeight - halfH) / 2;
    const int bottom = top + halfH - 1;
    const int midY = top + (halfH / 2);
    const int maxW = 4;

    auto drawArrow = [&](bool rightSide) {
        int baseX = rightSide ? (rectX + rectWidth + offset) : (rectX - offset - 1);

        for (int yy = top; yy <= bottom; yy++) {
            int dist = abs(yy - midY);
            int lineW = maxW - (dist * maxW / (halfH / 2));
            if (lineW < 1)
                lineW = 1;

            if (rightSide) {
                display->drawHorizontalLine(baseX, yy, lineW);
            } else {
                display->drawHorizontalLine(baseX - lineW + 1, yy, lineW);
            }
        }
    };

    // Right arrow
    if (navBarVisible && pageEnd < totalIcons) {
        int baseX = rectX + rectWidth + offset;
        int regionX = baseX;

#if GRAPHICS_TFT_COLORING_ENABLED
        registerTFTColorRegion(TFTColorRole::NavigationArrow, regionX, top, maxW, halfH);
#endif

        drawArrow(true);
    }

    // Left arrow
    if (navBarVisible && pageStart > 0) {
        int baseX = rectX - offset - 1;
        int regionX = baseX - maxW + 1;

#if GRAPHICS_TFT_COLORING_ENABLED
        registerTFTColorRegion(TFTColorRole::NavigationArrow, regionX, top, maxW, halfH);
#endif

        drawArrow(false);
    }

    // Knock the corners off the square
#if GRAPHICS_TFT_COLORING_ENABLED
    // TFT corner mask
    registerTFTColorRegion(TFTColorRole::NavigationArrow, rectX, rectY, 1, 1);
    registerTFTColorRegion(TFTColorRole::NavigationArrow, rectX + rectWidth - 1, rectY, 1, 1);
#else
    // monochrome styling only
    display->setColor(BLACK);
    display->drawRect(rectX, rectY, 1, 1);
    display->drawRect(rectX + rectWidth - 1, rectY, 1, 1);
    display->setColor(WHITE);
#endif
}

void UIRenderer::drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *message)
{
    uint16_t x_offset = display->width() / 2;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x_offset + x, 26 + y, message);
}

std::string UIRenderer::drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds)
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

int UIRenderer::measureStringWithEmotes(OLEDDisplay *display, const char *line, int emoteSpacing)
{
    return graphics::EmoteRenderer::measureStringWithEmotes(display, line, graphics::emotes, graphics::numEmotes, emoteSpacing);
}

size_t UIRenderer::truncateStringWithEmotes(OLEDDisplay *display, const char *line, char *out, size_t outSize, int maxWidth,
                                            const char *ellipsis, int emoteSpacing)
{
    return graphics::EmoteRenderer::truncateToWidth(display, line, out, outSize, maxWidth, ellipsis, graphics::emotes,
                                                    graphics::numEmotes, emoteSpacing);
}

void UIRenderer::drawStringWithEmotes(OLEDDisplay *display, int x, int y, const char *line, int fontHeight, int emoteSpacing,
                                      bool fauxBold)
{
    graphics::EmoteRenderer::drawStringWithEmotes(display, x, y, line, fontHeight, graphics::emotes, graphics::numEmotes,
                                                  emoteSpacing, fauxBold);
}

} // namespace graphics

#endif // HAS_SCREEN
