#include "graphics/Backlight.h"

#if HAS_PWM_BACKLIGHT

#include "mesh/NodeDB.h"

namespace graphics
{
namespace
{
bool pinConfigured = false;

// Level restored when the backlight is switched back on after being toggled off.
uint8_t lastOnLevel = PWM_BACKLIGHT_DEFAULT;

void drive(uint8_t level)
{
    if (!pinConfigured) {
        pinMode(PIN_PWM_BACKLIGHT, OUTPUT);
        pinConfigured = true;
    }
    analogWrite(PIN_PWM_BACKLIGHT, level);
}
} // namespace

void backlightSet(uint8_t level)
{
    if (level > 0)
        lastOnLevel = level;
    uiconfig.screen_brightness = level;
    drive(level);
}

uint8_t backlightGet()
{
    return uiconfig.screen_brightness;
}

void backlightOn()
{
    drive(uiconfig.screen_brightness);
}

void backlightOff()
{
    drive(0);
}

void backlightToggle()
{
    backlightSet(uiconfig.screen_brightness > 0 ? 0 : lastOnLevel);
}

void backlightStepUp()
{
    uint16_t raised = (uint16_t)uiconfig.screen_brightness + PWM_BACKLIGHT_STEP;
    backlightSet(raised > PWM_BACKLIGHT_MAX ? PWM_BACKLIGHT_MAX : (uint8_t)raised);
}

void backlightStepDown()
{
    // Leave an off backlight off; otherwise clamp at the minimum.
    if (uiconfig.screen_brightness == 0)
        return;
    backlightSet(uiconfig.screen_brightness <= PWM_BACKLIGHT_MIN + PWM_BACKLIGHT_STEP
                     ? PWM_BACKLIGHT_MIN
                     : uiconfig.screen_brightness - PWM_BACKLIGHT_STEP);
}
} // namespace graphics

#endif // HAS_PWM_BACKLIGHT
