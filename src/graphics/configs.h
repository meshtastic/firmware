#pragma once

#include "fonts.h"

#define FONT_HEIGHT 14 // actually 13 for "Arial 10" but want a little extra space
#define FONT_HEIGHT_16 (ArialMT_Plain_16[1] + 1)
// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TRANSITION_FRAMERATE 30 // fps
#define IDLE_FRAMERATE 1        // in fps
#define COMPASS_DIAM 44

// DEBUG
#define NUM_EXTRA_FRAMES 2 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS