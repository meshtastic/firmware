#ifndef WIFIOTA_H
#define WIFIOTA_H

#include "mesh-pb-constants.h"
#include <Arduino.h>

namespace WiFiOTA
{
void initialize();
bool isUpdated();

void recoverConfig(meshtastic_Config_NetworkConfig *network);
void saveConfig(meshtastic_Config_NetworkConfig *network);
bool trySwitchToOTA();
String getVersion();
} // namespace WiFiOTA

#endif // WIFIOTA_H
