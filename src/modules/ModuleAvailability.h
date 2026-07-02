#pragma once

#include "mesh/generated/meshtastic/config.pb.h"

inline bool isAudioModuleAvailable(bool isEsp32, bool hasSx1280, bool audioExcluded,
                                   meshtastic_Config_LoRaConfig_RegionCode region)
{
    return isEsp32 && hasSx1280 && !audioExcluded && region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
}

inline bool isAudioModuleAvailableForRegion(meshtastic_Config_LoRaConfig_RegionCode region)
{
#if defined(ARCH_ESP32)
    constexpr bool isEsp32 = true;
#else
    constexpr bool isEsp32 = false;
#endif

#if defined(USE_SX1280)
    constexpr bool hasSx1280 = true;
#else
    constexpr bool hasSx1280 = false;
#endif

#if MESHTASTIC_EXCLUDE_AUDIO
    constexpr bool audioExcluded = true;
#else
    constexpr bool audioExcluded = false;
#endif

    return isAudioModuleAvailable(isEsp32, hasSx1280, audioExcluded, region);
}
