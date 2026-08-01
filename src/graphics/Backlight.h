#pragma once

#include "configuration.h"

// PWM backlight control. A variant opts in by defining PIN_PWM_BACKLIGHT, optionally with
// PWM_BACKLIGHT_DEFAULT, _MIN, _MAX and _STEP. Levels are 0..255 in uiconfig.screen_brightness.

#if defined(PIN_PWM_BACKLIGHT)
#define HAS_PWM_BACKLIGHT 1
#else
#define HAS_PWM_BACKLIGHT 0
#endif

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

namespace graphics
{
void backlightSet(uint8_t level);

uint8_t backlightGet();

void backlightOn(); // drive the stored level

void backlightOff(); // drive 0, leaving the stored level alone

void backlightToggle();

void backlightStepUp();

void backlightStepDown();
} // namespace graphics

#endif // HAS_PWM_BACKLIGHT
