#ifndef WIFIOTA_H
#define WIFIOTA_H

#include <Arduino.h>
#include "mesh-pb-constants.h"

namespace WiFiOTA
{
void initialize();
bool isUpdated();

void recoverConfig(meshtastic_Config_NetworkConfig *network);
void saveConfig(meshtastic_Config_NetworkConfig *network);
bool trySwitchToOTA();
String getVersion();
}

#endif // WIFIOTA_H
