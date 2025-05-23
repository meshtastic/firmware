#include "DisplayFormatters.h"

const char *DisplayFormatters::getModemPresetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset, bool useShortName)
{
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