#pragma once

#include <stdint.h>

enum RadioModulation : uint8_t { MODULATION_LORA = 0, MODULATION_GFSK, MODULATION_FLRC };

/// Physical parameters of the FLRC / GFSK presets: SX128x at 2.4 GHz (FLRC and GFSK), SX126x sub-GHz (GFSK).
struct FastModemParams {
    RadioModulation modulation = MODULATION_LORA;
    uint16_t bitrateKbps = 0; // raw on-air bit rate
    float bwKHz = 0;          // channel width, used for channel planning
    uint8_t crNum = 1;        // FLRC coding rate crNum/crDen (1/1 for GFSK)
    uint8_t crDen = 1;
    int8_t sensitivityDbm = 0; // datasheet (SX1280: PER 1 %; SX1262: BER 0.1 %, interpolated between its rows)
    float freqDevKHz = 0;      // sub-GHz GFSK deviation (the SX128x derives its own from the bit rate)
    float rxBwKHz = 0;         // sub-GHz GFSK receiver filter, DSB (the SX128x picks its own from the bit rate)
};
