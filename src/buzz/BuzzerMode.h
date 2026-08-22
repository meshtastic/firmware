#pragma once

#include "mesh/generated/meshtastic/config.pb.h"

inline bool buzzerModeAllowsSystemTones(meshtastic_Config_DeviceConfig_BuzzerMode mode)
{
    return mode == meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED ||
           mode == meshtastic_Config_DeviceConfig_BuzzerMode_SYSTEM_ONLY;
}

inline bool buzzerModeAllowsNotification(meshtastic_Config_DeviceConfig_BuzzerMode mode, bool isDirectMessage)
{
    switch (mode) {
    case meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED:
    case meshtastic_Config_DeviceConfig_BuzzerMode_NOTIFICATIONS_ONLY:
        return true;
    case meshtastic_Config_DeviceConfig_BuzzerMode_DIRECT_MSG_ONLY:
        return isDirectMessage;
    default:
        return false;
    }
}

inline bool buzzerModeAllowsAnyNotification(meshtastic_Config_DeviceConfig_BuzzerMode mode)
{
    return buzzerModeAllowsNotification(mode, true);
}
