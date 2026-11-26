#pragma once

#include "power/HLT331Parser.h"
#include "power/PowerBank.h"

// Everything the Mesh Pocket V2 knows about its battery at one moment.
struct HLT331Sources {
    bool gaugeValid = false;
    uint16_t gaugeMv = 0;
    uint8_t gaugePercent = 0;

    bool haveFrame = false; // the HLT331 has sent at least one frame
    bool fresh = false;     // ...and one recently
    bool asleep = false;    // IO2 low: the HLT331 has gone quiet because nothing is plugged in
    HLT331Status controller;
};

// The gauge and the HLT331 measure the same cell; past this they cannot both be right.
constexpr uint16_t HLT331_GAUGE_MAX_DISAGREEMENT_MV = 200;

// Battery from the gauge, else from the HLT331's last frame if it is fresh or the HLT331 is asleep.
// Charging and external power only from a fresh frame. False if no battery source is usable.
bool hlt331MergeStatus(const HLT331Sources &in, PowerBankStatus &out);

// The page's view of a frame; live is false while the HLT331 is asleep or silent.
void hlt331Detail(const HLT331Status &s, bool live, PowerBankDetail &out);

const char *hlt331FastChargeName(uint8_t mode);
const char *hlt331WirelessProtocolName(uint8_t protocol);
