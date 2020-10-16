#pragma once

#include "fonts.h"

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define TRANSITION_FRAMERATE 30 // fps
#define IDLE_FRAMERATE 1        // in fps
#define COMPASS_DIAM 44

// DEBUG
#define NUM_EXTRA_FRAMES 2 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS