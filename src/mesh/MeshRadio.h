#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"


meshtastic_Config_LoRaConfig_ModemPreset PRESETS_STD[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO
};
meshtastic_Config_LoRaConfig_ModemPreset PRESETS_EU_868[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE
};
meshtastic_Config_LoRaConfig_ModemPreset PRESETS_LITE[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW
};

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_NARROW[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW
};

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_HAM[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_HAM_FAST,
};

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_UNDEF[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
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
    // static list of available presets
    const meshtastic_Config_LoRaConfig_ModemPreset *availablePresets;
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