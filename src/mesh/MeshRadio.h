#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"

struct RegionPresetBits {
    uint16_t allowPreset_LONG_FAST : 1;
    uint16_t allowPreset_LONG_SLOW : 1;
    uint16_t allowPreset_VERY_LONG_SLOW : 1; // Deprecated
    uint16_t allowPreset_MEDIUM_SLOW : 1;
    uint16_t allowPreset_MEDIUM_FAST : 1;
    uint16_t allowPreset_SHORT_SLOW : 1;
    uint16_t allowPreset_SHORT_FAST : 1;
    uint16_t allowPreset_LONG_MODERATE : 1;
    uint16_t allowPreset_SHORT_TURBO : 1; // 500kHz BW
    uint16_t allowPreset_LONG_TURBO : 1;  // 500kHz BW
    uint16_t allowPreset_LITE_FAST : 1;   // For EU_866
    uint16_t allowPreset_LITE_SLOW : 1;   // For EU_866
    uint16_t allowPreset_NARROW_FAST : 1; // Narrow BW
    uint16_t allowPreset_NARROW_SLOW : 1; // Narrow BW
    uint16_t allowPreset_HAM_FAST : 1;    // 500kHz BW
    uint16_t reserved : 1;
};

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