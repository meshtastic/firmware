#include "graphics/Backlight.h"

#if HAS_BACKLIGHT

#include "mesh/NodeDB.h"

#if HAS_GPIO_BACKLIGHT && defined(PCA_PIN_EINK_EN)
#include "main.h" // the GPIO expander the rail hangs off
#endif

namespace graphics
{
namespace
{
bool initialized = false;

// What the hardware is driven at right now, which is not the stored level while the screen is off.
uint8_t litLevel = 0;

// Level restored when the backlight is switched back on after being toggled off.
#if HAS_PWM_BACKLIGHT
uint8_t lastOnLevel = PWM_BACKLIGHT_DEFAULT;
#else
uint8_t lastOnLevel = GPIO_BACKLIGHT_ON_LEVEL;
#endif

void drive(uint8_t level)
{
    litLevel = level;
#if HAS_PWM_BACKLIGHT
    analogWrite(PIN_PWM_BACKLIGHT, level);
#elif defined(PIN_EINK_EN)
    digitalWrite(PIN_EINK_EN, level > 0 ? HIGH : LOW);
#elif defined(PCA_PIN_EINK_EN)
    io.digitalWrite(PCA_PIN_EINK_EN, level > 0 ? HIGH : LOW);
#endif
}
} // namespace

void backlightInit()
{
    if (initialized)
        return;
    initialized = true;

#if HAS_PWM_BACKLIGHT
    pinMode(PIN_PWM_BACKLIGHT, OUTPUT);
#elif defined(PIN_EINK_EN)
    pinMode(PIN_EINK_EN, OUTPUT);
#endif
    // PCA_PIN_EINK_EN is already configured by the variant's earlyInitVariant()

#if HAS_GPIO_BACKLIGHT
    // Any level that is not ours, such as the legacy 153 default, was not set here, so fall back to
    // the variant default rather than reading it as "lit".
    if (uiconfig.screen_brightness != 0 && uiconfig.screen_brightness != GPIO_BACKLIGHT_ON_LEVEL)
        uiconfig.screen_brightness = GPIO_BACKLIGHT_DEFAULT_LEVEL;
#endif

    // So a toggle or a momentary press restores the level the user last chose, not the compiled default
    if (uiconfig.screen_brightness > 0)
        lastOnLevel = uiconfig.screen_brightness;

    drive(uiconfig.screen_brightness);
}

void backlightSet(uint8_t level)
{
#if HAS_GPIO_BACKLIGHT
    // The rail has no intermediate states, so keep the stored level to the two this backend drives
    level = level > 0 ? GPIO_BACKLIGHT_ON_LEVEL : 0;
#endif
    if (level > 0)
        lastOnLevel = level;
    uiconfig.screen_brightness = level;
    drive(level);
}

uint8_t backlightGet()
{
    return uiconfig.screen_brightness;
}

bool backlightIsLit()
{
    return litLevel > 0;
}

void backlightOn()
{
    drive(uiconfig.screen_brightness);
}

void backlightMomentaryOn()
{
    drive(lastOnLevel);
}

void backlightOff()
{
    drive(0);
}

void backlightToggle()
{
    backlightSet(uiconfig.screen_brightness > 0 ? 0 : lastOnLevel);
}

#if HAS_PWM_BACKLIGHT
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
#endif
} // namespace graphics

#endif // HAS_BACKLIGHT
