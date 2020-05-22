#pragma once

#include "screen.h"

extern bool axp192_found;
extern bool ssd1306_found;
extern bool isCharging;
extern bool isUSBPowered;

// Global Screen singleton.
extern meshtastic::Screen screen;

// Return a human readable string of the form "Meshtastic_ab13"
const char *getDeviceName();

void getMacAddr(uint8_t *dmac);

void nrf52Setup(), esp32Setup(), nrf52Loop(), esp32Loop();