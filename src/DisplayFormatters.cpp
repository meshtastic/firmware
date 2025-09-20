#include "DisplayFormatters.h"

const char *DisplayFormatters::getModemPresetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset, bool useShortName,
                                                         bool usePreset)
{

    // If use_preset is false, always return "Custom"
    if (!usePreset) {
        return "Custom";
    }

    switch (preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        return useShortName ? "ShortT" : "ShortTurbo";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        return useShortName ? "ShortS" : "ShortSlow";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        return useShortName ? "ShortF" : "ShortFast";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        return useShortName ? "MedS" : "MediumSlow";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        return useShortName ? "MedF" : "MediumFast";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        return useShortName ? "LongS" : "LongSlow";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
        return useShortName ? "LongF" : "LongFast";
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        return useShortName ? "LongM" : "LongMod";
        break;
    default:
        return useShortName ? "Custom" : "Invalid";
        break;
    }
}

const char *DisplayFormatters::getDeviceRole(meshtastic_Config_DeviceConfig_Role role)
{
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_CLIENT:
        return "Client";
        break;
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
        return "Client Mute";
        break;
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:
        return "Client Hidden";
        break;
    case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:
        return "Client Base";
        break;
    case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND:
        return "Lost and Found";
        break;
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
        return "Tracker";
        break;
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
        return "Sensor";
        break;
    case meshtastic_Config_DeviceConfig_Role_TAK:
        return "TAK";
        break;
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
        return "TAK Tracker";
        break;
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
        return "Router";
        break;
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
        return "Router Late";
        break;
    case meshtastic_Config_DeviceConfig_Role_REPEATER:
        return "Repeater";
        break;
    default:
        return "Unknown";
        break;
    }
}