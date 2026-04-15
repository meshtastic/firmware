#include "DisplayFormatters.h"
#include "MeshRadio.h"

const char *DisplayFormatters::getModemPresetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset, bool useShortName,
                                                         bool usePreset)
{

    // If use_preset is false, always return "Custom" — callers such as RadioInterface and Channels
    // rely on this being a stable literal for channel-name hashing and default-channel detection.
    if (!usePreset) {
        return "Custom";
    }

    switch (preset) {
    case PRESET(SHORT_TURBO):
        return useShortName ? "ShortT" : "ShortTurbo";
        break;
    case PRESET(SHORT_SLOW):
        return useShortName ? "ShortS" : "ShortSlow";
        break;
    case PRESET(SHORT_FAST):
        return useShortName ? "ShortF" : "ShortFast";
        break;
    case PRESET(MEDIUM_SLOW):
        return useShortName ? "MedS" : "MediumSlow";
        break;
    case PRESET(MEDIUM_FAST):
        return useShortName ? "MedF" : "MediumFast";
        break;
    case PRESET(LONG_SLOW):
        return useShortName ? "LongS" : "LongSlow";
        break;
    case PRESET(LONG_FAST):
        return useShortName ? "LongF" : "LongFast";
        break;
    case PRESET(LONG_TURBO):
        return useShortName ? "LongT" : "LongTurbo";
        break;
    case PRESET(LONG_MODERATE):
        return useShortName ? "LongM" : "LongMod";
        break;
    case PRESET(LITE_FAST):
        return useShortName ? "LiteF" : "LiteFast";
        break;
    case PRESET(LITE_SLOW):
        return useShortName ? "LiteS" : "LiteSlow";
        break;
    case PRESET(NARROW_FAST):
        return useShortName ? "NarF" : "NarrowFast";
        break;
    case PRESET(NARROW_SLOW):
        return useShortName ? "NarS" : "NarrowSlow";
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
    default:
        return "Unknown";
        break;
    }
}