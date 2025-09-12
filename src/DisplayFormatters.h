#pragma once
#include "NodeDB.h"

class DisplayFormatters
{
  public:
    static const char *getModemPresetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset, bool useShortName,
                                                 bool usePreset);

  public:
    static const char *getDeviceRole(meshtastic_Config_DeviceConfig_Role role);
};
