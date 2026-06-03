#include "configuration.h"
#if HAS_SCREEN
#include "CompassRenderer.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include <cmath>

namespace graphics
{
namespace CompassRenderer
{
void drawCompassNorth(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading, int16_t radius)
{
    if (currentResolution == ScreenResolution::High) {
        radius += 4;
    }

    const float northAngle = (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING) ? -myHeading : 0.0f;
    const int16_t nX = compassX + static_cast<int16_t>((radius - 1) * sinf(northAngle));
    const int16_t nY = compassY - static_cast<int16_t>((radius - 1) * cosf(northAngle));

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
#if !GRAPHICS_TFT_COLORING_ENABLED
    display->setColor(BLACK);
    const int16_t nLabelWidth = display->getStringWidth("N");
    if (currentResolution == ScreenResolution::High) {
        display->fillRect(nX - 8, nY - 1, nLabelWidth + 3, FONT_HEIGHT_SMALL - 6);
    } else {
        display->fillRect(nX - 4, nY - 1, nLabelWidth + 2, FONT_HEIGHT_SMALL - 6);
    }
#endif
    display->setColor(WHITE);
    display->drawString(nX, nY - 3, "N");
}

void drawArrowToNode(OLEDDisplay *display, int16_t x, int16_t y, int16_t size, float bearing)
{
    const float radians = bearing * DEG_TO_RAD;
    const float sinA = sinf(radians);
    const float cosA = cosf(radians);
    const float tipHalf = size * 0.5f;
    const float lx = -(size / 6.0f);
    const float ly = size / 4.0f;
    const float rx = (size / 6.0f);
    const float ry = size / 4.0f;
    const float tx = 0.0f;
    const float ty = size / 4.5f;

    const int16_t tipX = static_cast<int16_t>(x + (tipHalf * sinA));
    const int16_t tipY = static_cast<int16_t>(y - (tipHalf * cosA));
    const int16_t leftX = static_cast<int16_t>(x + (lx * cosA) - (ly * sinA));
    const int16_t leftY = static_cast<int16_t>(y + (lx * sinA) + (ly * cosA));
    const int16_t rightX = static_cast<int16_t>(x + (rx * cosA) - (ry * sinA));
    const int16_t rightY = static_cast<int16_t>(y + (rx * sinA) + (ry * cosA));
    const int16_t tailX = static_cast<int16_t>(x + (tx * cosA) - (ty * sinA));
    const int16_t tailY = static_cast<int16_t>(y + (tx * sinA) + (ty * cosA));

    display->fillTriangle(tipX, tipY, leftX, leftY, tailX, tailY);
    display->fillTriangle(tipX, tipY, rightX, rightY, tailX, tailY);
}

void drawNodeHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, uint16_t compassDiam, float headingRadian)
{
    const int16_t size = static_cast<int16_t>(compassDiam * 0.6f);
    drawArrowToNode(display, compassX, compassY, size, headingRadian * RAD_TO_DEG);
}

bool getHeadingRadians(double lat, double lon, float &headingRadian)
{
    headingRadian = 0.0f;

    if (uiconfig.compass_mode == meshtastic_CompassMode_FREEZE_HEADING)
        return true;

    if (!screen)
        return false;

    if (screen->hasHeading()) {
        headingRadian = screen->getHeading() * DEG_TO_RAD;
        return true;
    }

    const float estimatedHeadingDeg = screen->estimatedHeading(lat, lon);
    if (!(estimatedHeadingDeg >= 0.0f))
        return false;

    headingRadian = estimatedHeadingDeg * DEG_TO_RAD;
    return true;
}

float adjustBearingForCompassMode(float bearingRadian, float headingRadian)
{
    if (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING)
        return bearingRadian - headingRadian;

    return bearingRadian;
}

float radiansToDegrees360(float angleRadian)
{
    constexpr float fullTurnDeg = 360.0f;
    float degrees = angleRadian * RAD_TO_DEG;
    if (degrees < 0.0f)
        degrees += fullTurnDeg;
    else if (degrees >= fullTurnDeg)
        degrees -= fullTurnDeg;
    return degrees;
}

uint16_t getCompassDiam(uint32_t displayWidth, uint32_t displayHeight)
{
    // Calculate appropriate compass diameter based on display size
    uint16_t minDimension = (displayWidth < displayHeight) ? displayWidth : displayHeight;
    uint16_t maxDiam = minDimension / 3; // Use 1/3 of the smaller dimension

    // Ensure minimum and maximum bounds
    if (maxDiam < 16)
        maxDiam = 16;
    if (maxDiam > 64)
        maxDiam = 64;

    return maxDiam;
}

} // namespace CompassRenderer
} // namespace graphics
#endif
