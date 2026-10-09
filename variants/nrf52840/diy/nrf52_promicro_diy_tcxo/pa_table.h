#pragma once
#include "RadioLib.h"

// LR2021 LF PA from LR20xx DS rev 2.2 Table 7-19 (915 MHz reference design) at 10..22 dBm; that table stops at 10,
// so -9..9 keep RadioLib's entries. RadioLib's own table never programs tx_power above 20.5 dBm and flattens from 14.
// Index = power_dBm + 9. paVal in 0.5 dB. Used at 500-1000 MHz only (isLr20x0CustomLfPaBand); RadioLib default elsewhere.

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
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 20 }, //  1
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 23 }, //  2
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 27 }, //  3
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 33 }, //  4
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 26 }, //  5
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 31 }, //  6
    { .paDutyCycle = 1, .paSlices = 3, .paVal = 27 }, //  7
    { .paDutyCycle = 1, .paSlices = 1, .paVal = 37 }, //  8
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 40 }, //  9
    { .paDutyCycle = 1, .paSlices = 2, .paVal = 32 }, // 10  DS: tx_power 16
    { .paDutyCycle = 2, .paSlices = 4, .paVal = 30 }, // 11  DS: 15
    { .paDutyCycle = 4, .paSlices = 2, .paVal = 30 }, // 12  DS: 15
    { .paDutyCycle = 4, .paSlices = 2, .paVal = 32 }, // 13  DS: 16
    { .paDutyCycle = 4, .paSlices = 2, .paVal = 34 }, // 14  DS: 17
    { .paDutyCycle = 4, .paSlices = 5, .paVal = 34 }, // 15  DS: 17
    { .paDutyCycle = 7, .paSlices = 3, .paVal = 34 }, // 16  DS: 17
    { .paDutyCycle = 7, .paSlices = 3, .paVal = 36 }, // 17  DS: 18
    { .paDutyCycle = 5, .paSlices = 7, .paVal = 38 }, // 18  DS: 19
    { .paDutyCycle = 5, .paSlices = 7, .paVal = 40 }, // 19  DS: 20
    { .paDutyCycle = 5, .paSlices = 6, .paVal = 42 }, // 20  DS: 21
    { .paDutyCycle = 5, .paSlices = 6, .paVal = 44 }, // 21  DS: 22
    { .paDutyCycle = 7, .paSlices = 7, .paVal = 44 }, // 22  DS: 22
    // clang-format on
};
