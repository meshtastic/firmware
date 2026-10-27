#pragma once

#include <stdint.h>

enum RadioModulation : uint8_t { MODULATION_LORA = 0, MODULATION_GFSK, MODULATION_FLRC };

/// Physical parameters of the SX128x FLRC / GFSK presets (LORA_24 region only).
struct FastModemParams {
    RadioModulation modulation = MODULATION_LORA;
    uint16_t bitrateKbps = 0; // raw on-air bit rate
    float bwKHz = 0;          // occupied bandwidth, used for channel planning
    uint8_t crNum = 1;        // FLRC coding rate crNum/crDen (1/1 for GFSK)
    uint8_t crDen = 1;
    int8_t sensitivityDbm = 0; // SX1280 datasheet, PER 1 %
};
