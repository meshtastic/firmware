#ifndef MESHTASTICOTA_H
#define MESHTASTICOTA_H

#include "mesh-pb-constants.h"
#include <Arduino.h>

namespace MeshtasticOTA
{
void initialize();
bool isUpdated();

void recoverConfig(meshtastic_Config_NetworkConfig *network);
void saveConfig(meshtastic_Config_NetworkConfig *network, meshtastic_OTAMode method, uint8_t *ota_hash);
bool trySwitchToOTA();
const char *getVersion();
} // namespace MeshtasticOTA

#endif // MESHTASTICOTA_H
