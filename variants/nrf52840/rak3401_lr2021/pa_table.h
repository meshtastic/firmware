#pragma once
#include "RadioLib.h"

// RAK13700 LF PA - mid fine-tune: 8 high (+0.7), 10 low (-0.5); rest within ~0.6 dB.
// Index = power_dBm + 9. paVal in 0.5 dB.
// HF (2.4 GHz) uses RadioLib default PA table (LORA_24 region caps at 10 dBm).

static LR2021PaTableEntry_t lr2021_pa_table_lf[RADIOLIB_LR2021_PA_TABLE_LEN] = {
    // clang-format off
    { .paDutyCycle = 1, .paSlices = 1, .paVal =  8 }, // -9
    { .paDutyCycle = 2, .paSlices = 2, .paVal =  1 }, // -8
    { .paDutyCycle = 2, .paSlices = 2, .paVal =  3 }, // -7
    { .paDutyCycle = 2, .paSlices = 2, .paVal =  5 }, // -6
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 13 }, // -5
    { .paDutyCycle = 2, .paSlices = 1, .paVal = 13 }, // -4
    { .paDutyCycle = 2, .paSlices = 2, .paVal = 11 }, // -3
    { .paDutyCycle = 2, .paSlices = 2, .paVal = 13 }, // -2
    { .paDutyCycle = 3, .paSlices = 1, .paVal = 12 }, // -1
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 18 }, //  0
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 18 }, //  1
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 16 }, //  2
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 20 }, //  3
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 22 }, //  4
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 22 }, //  5
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 24 }, //  6
    { .paDutyCycle = 1, .paSlices = 3, .paVal = 27 }, //  7  was 28
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 30 }, //  8  was 32; meas 8.7 want ~8
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 32 }, //  9  was 33
    { .paDutyCycle = 2, .paSlices = 2, .paVal = 33 }, // 10  was 35; meas 11 want ~10
    { .paDutyCycle = 2, .paSlices = 2, .paVal = 35 }, // 11
    { .paDutyCycle = 2, .paSlices = 3, .paVal = 35 }, // 12
    { .paDutyCycle = 2, .paSlices = 5, .paVal = 37 }, // 13
    { .paDutyCycle = 3, .paSlices = 2, .paVal = 38 }, // 14
    { .paDutyCycle = 3, .paSlices = 3, .paVal = 39 }, // 15
    { .paDutyCycle = 3, .paSlices = 6, .paVal = 38 }, // 16
    { .paDutyCycle = 4, .paSlices = 4, .paVal = 40 }, // 17
    { .paDutyCycle = 4, .paSlices = 5, .paVal = 41 }, // 18
    { .paDutyCycle = 4, .paSlices = 7, .paVal = 43 }, // 19
    { .paDutyCycle = 5, .paSlices = 4, .paVal = 44 }, // 20
    { .paDutyCycle = 5, .paSlices = 6, .paVal = 44 }, // 21
    { .paDutyCycle = 6, .paSlices = 7, .paVal = 44 }, // 22
    // clang-format on
};
