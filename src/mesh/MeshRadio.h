#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"

// TODO: make this reduce to a uint16_t bitfield?

struct RegionPresetBits {
    bool allowPreset_LONG_FAST;
    bool allowPreset_LONG_SLOW;
    bool allowPreset_VERY_LONG_SLOW; // Deprecated
    bool allowPreset_MEDIUM_SLOW;
    bool allowPreset_MEDIUM_FAST;
    bool allowPreset_SHORT_SLOW;
    bool allowPreset_SHORT_FAST;
    bool allowPreset_LONG_MODERATE;
    bool allowPreset_SHORT_TURBO; // 500kHz BW
    bool allowPreset_LONG_TURBO;  // 500kHz BW
    bool allowPreset_LITE_FAST;   // For EU_866
    bool allowPreset_LITE_SLOW;   // For EU_866
    bool allowPreset_NARROW_FAST; // Narrow BW
    bool allowPreset_NARROW_SLOW; // Narrow BW
    bool allowPreset_HAM_FAST;    // 500kHz BW
    bool reserved;
};

constexpr RegionPresetBits PRESETS_STD = {0b110111111100000};
constexpr RegionPresetBits PRESETS_EU_868 = {0b110111110000000};
constexpr RegionPresetBits PRESETS_LITE = {0b0000000000110000};
constexpr RegionPresetBits PRESETS_NARROW = {0b0000000000001100};

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
    meshtastic_Config_LoRaConfig_ModemPreset defaultPreset;
    RegionPresetBits presetBits;
    const char *name; // EU433 etc
};

extern const RegionInfo regions[];
extern const RegionInfo *myRegion;

extern void initRegion();

/**
 * Get the effective duty cycle for the current region based on device role.
 * For EU_866, returns 10% for fixed devices (ROUTER, ROUTER_LATE) and 2.5% for mobile devices.
 * For other regions, returns the standard duty cycle.
 */
extern float getEffectiveDutyCycle();