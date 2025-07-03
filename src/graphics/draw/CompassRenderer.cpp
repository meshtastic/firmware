#include "CompassRenderer.h"
#include "NodeDB.h"
#include "UIRenderer.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include <cmath>

namespace graphics
{
namespace CompassRenderer
{

// Point helper class for compass calculations
struct Point {
    float x, y;
    Point(float x, float y) : x(x), y(y) {}

    void rotate(float angle)
    {
        float cos_a = cos(angle);
        float sin_a = sin(angle);
        float new_x = x * cos_a - y * sin_a;
        float new_y = x * sin_a + y * cos_a;
        x = new_x;
        y = new_y;
    }

    void scale(float factor)
    {
        x *= factor;
        y *= factor;
    }

    void translate(float dx, float dy)
    {
        x += dx;
        y += dy;
    }
};

void drawCompassNorth(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading, int16_t radius)
{
    // Show the compass heading (not implemented in original)
    // This could draw a "N" indicator or north arrow
    // For now, we'll draw a simple north indicator
    // const float radius = 17.0f;
    if (isHighResolution) {
        radius += 4;
    }
    Point north(0, -radius);
    if (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING)
        north.rotate(-myHeading);
    north.translate(compassX, compassY);

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setColor(BLACK);
    if (isHighResolution) {
        display->fillRect(north.x - 8, north.y - 1, display->getStringWidth("N") + 3, FONT_HEIGHT_SMALL - 6);
    } else {
        display->fillRect(north.x - 4, north.y - 1, display->getStringWidth("N") + 2, FONT_HEIGHT_SMALL - 6);
    }
    display->setColor(WHITE);
    display->drawString(north.x, north.y - 3, "N");
}

void drawNodeHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, uint16_t compassDiam, float headingRadian)
{
    Point tip(0.0f, -0.5f), tail(0.0f, 0.35f); // pointing up initially
    float arrowOffsetX = 0.14f, arrowOffsetY = 0.9f;
    Point leftArrow(tip.x - arrowOffsetX, tip.y + arrowOffsetY), rightArrow(tip.x + arrowOffsetX, tip.y + arrowOffsetY);

    Point *arrowPoints[] = {&tip, &tail, &leftArrow, &rightArrow};

    for (int i = 0; i < 4; i++) {
        arrowPoints[i]->rotate(headingRadian);
        arrowPoints[i]->scale(compassDiam * 0.6);
        arrowPoints[i]->translate(compassX, compassY);
    }

#ifdef USE_EINK
    display->drawTriangle(tip.x, tip.y, rightArrow.x, rightArrow.y, tail.x, tail.y);
#else
    display->fillTriangle(tip.x, tip.y, rightArrow.x, rightArrow.y, tail.x, tail.y);
#endif
    display->drawTriangle(tip.x, tip.y, leftArrow.x, leftArrow.y, tail.x, tail.y);
}

void drawArrowToNode(OLEDDisplay *display, int16_t x, int16_t y, int16_t size, float bearing)
{
    float radians = bearing * DEG_TO_RAD;

    Point tip(0, -size / 2);
    Point left(-size / 6, size / 4);
    Point right(size / 6, size / 4);
    Point tail(0, size / 4.5);

    tip.rotate(radians);
    left.rotate(radians);
    right.rotate(radians);
    tail.rotate(radians);

    tip.translate(x, y);
    left.translate(x, y);
    right.translate(x, y);
    tail.translate(x, y);

    display->fillTriangle(tip.x, tip.y, left.x, left.y, tail.x, tail.y);
    display->fillTriangle(tip.x, tip.y, right.x, right.y, tail.x, tail.y);
}

float estimatedHeading(double lat, double lon)
{
    // Simple magnetic declination estimation
    // This is a very basic implementation - the original might be more sophisticated
    return 0.0f; // Return 0 for now, indicating no heading available
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
