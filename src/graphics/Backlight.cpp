#include "graphics/Backlight.h"

#if HAS_PWM_BACKLIGHT

#include "mesh/NodeDB.h"

namespace graphics
{
namespace
{
bool pinConfigured = false;

uint8_t lastOnLevel = PWM_BACKLIGHT_DEFAULT;

void configurePin()
{
    if (!pinConfigured) {
        pinMode(PIN_PWM_BACKLIGHT, OUTPUT);
        pinConfigured = true;
    }
}
} // namespace

void backlightSet(uint8_t level)
{
    configurePin();
    if (level > 0)
        lastOnLevel = level;
    uiconfig.screen_brightness = level;
    analogWrite(PIN_PWM_BACKLIGHT, level);
}

uint8_t backlightGet()
{
    return uiconfig.screen_brightness;
}

void backlightOn()
{
    backlightSet(uiconfig.screen_brightness > 0 ? uiconfig.screen_brightness : lastOnLevel);
}

void backlightOff()
{
    backlightSet(0);
}

void backlightToggle()
{
    if (uiconfig.screen_brightness > 0)
        backlightOff();
    else
        backlightOn();
}

void backlightStepUp()
{
    uint16_t raised = (uint16_t)uiconfig.screen_brightness + PWM_BACKLIGHT_STEP;
    backlightSet(raised > PWM_BACKLIGHT_MAX ? PWM_BACKLIGHT_MAX : (uint8_t)raised);
}

void backlightStepDown()
{
    // Clamp at the minimum so stepping down never switches the backlight off.
    backlightSet(uiconfig.screen_brightness <= PWM_BACKLIGHT_MIN + PWM_BACKLIGHT_STEP
                     ? PWM_BACKLIGHT_MIN
                     : uiconfig.screen_brightness - PWM_BACKLIGHT_STEP);
}
} // namespace graphics

#endif // HAS_PWM_BACKLIGHT
