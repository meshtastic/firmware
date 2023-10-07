#pragma once

static const char *getModemPresetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset, bool useShortName = false)
{
    switch (preset) {
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
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
        return useShortName ? "VeryL" : "VLongSlow";
        break;
    default:
        return useShortName ? "Custom" : "Invalid";
        break;
    }
}

#ifdef ARCH_ESP32
inline static const char *getWifiDisconnectReasonName(uint8_t reasonCode)
{
    if (reasonCode == 2) {
        return "Authentication Invalid";
    } else if (reasonCode == 3) {
        return "De-authenticated";
    } else if (reasonCode == 4) {
        return "Disassociated Expired";
    } else if (reasonCode == 5) {
        return "AP - Too Many Clients";
    } else if (reasonCode == 6) {
        return "NOT_AUTHED";
    } else if (reasonCode == 7) {
        return "NOT_ASSOCED";
    } else if (reasonCode == 8) {
        return "Disassociated";
    } else if (reasonCode == 9) {
        return "ASSOC_NOT_AUTHED";
    } else if (reasonCode == 10) {
        return "DISASSOC_PWRCAP_BAD";
    } else if (reasonCode == 11) {
        return "DISASSOC_SUPCHAN_BAD";
    } else if (reasonCode == 13) {
        return "IE_INVALID";
    } else if (reasonCode == 14) {
        return "MIC_FAILURE";
    } else if (reasonCode == 15) {
        return "AP Handshake Timeout";
    } else if (reasonCode == 16) {
        return "GROUP_KEY_UPDATE_TIMEOUT";
    } else if (reasonCode == 17) {
        return "IE_IN_4WAY_DIFFERS";
    } else if (reasonCode == 18) {
        return "Invalid Group Cipher";
    } else if (reasonCode == 19) {
        return "Invalid Pairwise Cipher";
    } else if (reasonCode == 20) {
        return "AKMP_INVALID";
    } else if (reasonCode == 21) {
        return "UNSUPP_RSN_IE_VERSION";
    } else if (reasonCode == 22) {
        return "INVALID_RSN_IE_CAP";
    } else if (reasonCode == 23) {
        return "802_1X_AUTH_FAILED";
    } else if (reasonCode == 24) {
        return "CIPHER_SUITE_REJECTED";
    } else if (reasonCode == 200) {
        return "BEACON_TIMEOUT";
    } else if (reasonCode == 201) {
        return "AP Not Found";
    } else if (reasonCode == 202) {
        return "AUTH_FAIL";
    } else if (reasonCode == 203) {
        return "ASSOC_FAIL";
    } else if (reasonCode == 204) {
        return "HANDSHAKE_TIMEOUT";
    } else if (reasonCode == 205) {
        return "Connection Failed";
    } else {
        return "Unknown Status";
    }
}
#endif