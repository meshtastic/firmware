#pragma once

#include "configuration.h"

// Backlight control for a PWM rail (PIN_PWM_BACKLIGHT) or an on/off GPIO rail (PIN_EINK_EN,
// PCA_PIN_EINK_EN). uiconfig.screen_brightness is the configured level, not the live pin state.

#if defined(PIN_PWM_BACKLIGHT)
#define HAS_PWM_BACKLIGHT 1
#else
#define HAS_PWM_BACKLIGHT 0
#endif

// MINI_EPAPER_S3 names its panel power rail PIN_EINK_EN. That is not a backlight and has to stay
// powered for the panel to work, so EInkDisplay::connect() drives it instead.
#if !HAS_PWM_BACKLIGHT && (defined(PIN_EINK_EN) || defined(PCA_PIN_EINK_EN)) && !defined(MINI_EPAPER_S3)
#define HAS_GPIO_BACKLIGHT 1
#else
#define HAS_GPIO_BACKLIGHT 0
#endif

#define HAS_BACKLIGHT (HAS_PWM_BACKLIGHT || HAS_GPIO_BACKLIGHT)

#if HAS_PWM_BACKLIGHT

#ifndef PWM_BACKLIGHT_DEFAULT
#define PWM_BACKLIGHT_DEFAULT 128
#endif
#ifndef PWM_BACKLIGHT_MIN
#define PWM_BACKLIGHT_MIN 8
#endif
#ifndef PWM_BACKLIGHT_MAX
#define PWM_BACKLIGHT_MAX 248
#endif
#ifndef PWM_BACKLIGHT_STEP
#define PWM_BACKLIGHT_STEP 20
#endif

#endif // HAS_PWM_BACKLIGHT

#if HAS_GPIO_BACKLIGHT

// On or off only, so these are the sole levels this backend stores. A variant defines
// GPIO_BACKLIGHT_DEFAULT_ON to power up lit.
#define GPIO_BACKLIGHT_ON_LEVEL 255
#if defined(GPIO_BACKLIGHT_DEFAULT_ON)
#define GPIO_BACKLIGHT_DEFAULT_LEVEL GPIO_BACKLIGHT_ON_LEVEL
#else
#define GPIO_BACKLIGHT_DEFAULT_LEVEL 0
#endif

#endif // HAS_GPIO_BACKLIGHT

#if HAS_BACKLIGHT

namespace graphics
{
void backlightInit(); // configure the pin, settle the stored level, then drive it. Idempotent

void backlightSet(uint8_t level);

uint8_t backlightGet(); // configured level, unchanged by backlightOff()

bool backlightIsLit(); // what the hardware is being driven at right now

void backlightOn(); // drive the stored level

void backlightMomentaryOn(); // light it regardless of the stored level, for press-and-hold

void backlightOff(); // drive 0, leaving the stored level alone

void backlightToggle();

#if HAS_PWM_BACKLIGHT
void backlightStepUp();

void backlightStepDown();
#endif
} // namespace graphics

#endif // HAS_BACKLIGHT
