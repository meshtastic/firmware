#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"

// Map from old region names to new region enums
struct RegionInfo {
    meshtastic_Config_LoRaConfig_RegionCode code;
    float freqStart;
    float freqEnd;
    float dutyCycle;
    float spacing;
    uint8_t powerLimit; // Or zero for not set
    bool audioPermitted;
    bool freqSwitching;
    bool wideLora;
    const char *name; // EU433 etc
};

extern const RegionInfo regions[];
extern const RegionInfo *myRegion;

extern void initRegion();

// Valid LoRa spread factor range and defaults
constexpr uint8_t LORA_SF_MIN = 7;
constexpr uint8_t LORA_SF_MAX = 12;
constexpr uint8_t LORA_SF_DEFAULT = 11; // LONG_FAST default

// Valid LoRa coding rate range and default
constexpr uint8_t LORA_CR_MIN = 4;
constexpr uint8_t LORA_CR_MAX = 8;
constexpr uint8_t LORA_CR_DEFAULT = 5; // LONG_FAST default

// Default bandwidth in kHz (LONG_FAST)
constexpr float LORA_BW_DEFAULT_KHZ = 250.0f;

/// Clamp spread factor to the valid LoRa range [7, 12].
/// Out-of-range values (including 0 from unset preset mode) return LORA_SF_DEFAULT.
static inline uint8_t clampSpreadFactor(uint8_t sf)
{
    if (sf < LORA_SF_MIN || sf > LORA_SF_MAX)
        return LORA_SF_DEFAULT;
    return sf;
}

/// Clamp coding rate to the valid LoRa range [4, 8].
/// Out-of-range values return LORA_CR_DEFAULT.
static inline uint8_t clampCodingRate(uint8_t cr)
{
    if (cr < LORA_CR_MIN || cr > LORA_CR_MAX)
        return LORA_CR_DEFAULT;
    return cr;
}

/// Ensure bandwidth is positive. Non-positive values return LORA_BW_DEFAULT_KHZ.
static inline float clampBandwidthKHz(float bwKHz)
{
    if (bwKHz <= 0.0f)
        return LORA_BW_DEFAULT_KHZ;
    return bwKHz;
}

static inline float bwCodeToKHz(uint16_t bwCode)
{
    if (bwCode == 31)
        return 31.25f;
    if (bwCode == 62)
        return 62.5f;
    if (bwCode == 200)
        return 203.125f;
    if (bwCode == 400)
        return 406.25f;
    if (bwCode == 800)
        return 812.5f;
    if (bwCode == 1600)
        return 1625.0f;
    return (float)bwCode;
}

static inline uint16_t bwKHzToCode(float bwKHz)
{
    if (bwKHz > 31.24f && bwKHz < 31.26f)
        return 31;
    if (bwKHz > 62.49f && bwKHz < 62.51f)
        return 62;
    if (bwKHz > 203.12f && bwKHz < 203.13f)
        return 200;
    if (bwKHz > 406.24f && bwKHz < 406.26f)
        return 400;
    if (bwKHz > 812.49f && bwKHz < 812.51f)
        return 800;
    if (bwKHz > 1624.99f && bwKHz < 1625.01f)
        return 1600;
    return (uint16_t)(bwKHz + 0.5f);
}

static inline void modemPresetToParams(meshtastic_Config_LoRaConfig_ModemPreset preset, bool wideLora, float &bwKHz, uint8_t &sf,
                                       uint8_t &cr)
{
    switch (preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bwKHz = wideLora ? 1625.0f : 500.0f;
        cr = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bwKHz = wideLora ? 812.5f : 250.0f;
        cr = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bwKHz = wideLora ? 812.5f : 250.0f;
        cr = 5;
        sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bwKHz = wideLora ? 812.5f : 250.0f;
        cr = 5;
        sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bwKHz = wideLora ? 812.5f : 250.0f;
        cr = 5;
        sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        bwKHz = wideLora ? 1625.0f : 500.0f;
        cr = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bwKHz = wideLora ? 406.25f : 125.0f;
        cr = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bwKHz = wideLora ? 406.25f : 125.0f;
        cr = 8;
        sf = 12;
        break;
    default: // LONG_FAST (or illegal)
        bwKHz = wideLora ? 812.5f : 250.0f;
        cr = 5;
        sf = 11;
        break;
    }
}

static inline float modemPresetToBwKHz(meshtastic_Config_LoRaConfig_ModemPreset preset, bool wideLora)
{
    float bwKHz = 0;
    uint8_t sf = 0;
    uint8_t cr = 0;
    modemPresetToParams(preset, wideLora, bwKHz, sf, cr);
    return bwKHz;
}