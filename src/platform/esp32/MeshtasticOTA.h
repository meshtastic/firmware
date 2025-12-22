#ifndef MESHTASTICOTA_H
#define MESHTASTICOTA_H

#include "mesh-pb-constants.h"
#include <Arduino.h>

namespace MeshtasticOTA
{
void initialize();
bool isUpdated();

void recoverConfig(meshtastic_Config_NetworkConfig *network);
void saveConfig(meshtastic_Config_NetworkConfig *network, bool method);
bool trySwitchToOTA();
const char *getVersion();
} // namespace MeshtasticOTA

#endif // MESHTASTICOTA_H
