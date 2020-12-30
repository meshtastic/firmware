#pragma once

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "PointerQueue.h"
#include "configuration.h"

// Map from old region names to new region enums
struct RegionInfo {
    RegionCode code;
    uint8_t numChannels;
    uint8_t powerLimit; // Or zero for not set
    float freq;
    float spacing;
    const char *name; // EU433 etc
};

extern const RegionInfo regions[];
extern const RegionInfo *myRegion;

extern void initRegion();