#pragma once

#include "screen.h"

extern bool axp192_found;
extern bool ssd1306_found;
extern bool isCharging;
extern bool isUSBPowered;

// Global Screen singleton.
extern meshtastic::Screen screen;
