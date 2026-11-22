#pragma once

// Shadows device-ui's graphics/LGFX/LGFX_GENERIC.h, which DisplayDriverFactory.cpp includes under
// -D LGFX_DRIVER_TEMPLATE: the board class reaches device-ui without a fork.
#include "LGFX_T_DISPLAY_P4.h"
