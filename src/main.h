#pragma once

#include "graphics/Screen.h"
#include "PowerStatus.h"
#include "GPSStatus.h"
#include "NodeStatus.h"

extern bool axp192_found;
extern bool ssd1306_found;
extern bool isCharging;
extern bool isUSBPowered;

// Global Screen singleton.
extern graphics::Screen screen;
//extern Observable<meshtastic::PowerStatus> newPowerStatus; //TODO: move this to main-esp32.cpp somehow or a helper class

//extern meshtastic::PowerStatus *powerStatus;
//extern meshtastic::GPSStatus *gpsStatus;
//extern meshtastic::NodeStatusHandler *nodeStatusHandler;

// Return a human readable string of the form "Meshtastic_ab13"
const char *getDeviceName();



void nrf52Setup(), esp32Setup(), nrf52Loop(), esp32Loop();