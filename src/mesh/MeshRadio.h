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
    meshtastic_Config_LoRaConfig_ModemPreset defaultPreset;
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