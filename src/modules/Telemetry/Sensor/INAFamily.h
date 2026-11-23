#pragma once

#include <cstdint>

#ifndef INA219_MULTIPLIER
#define INA219_MULTIPLIER 1.0f
#endif
// Adafruit 904 / GY-219, and Adafruit_INA219 1.2.3's setCalibration_* all assume 0.1 ohm.
#ifndef INA219_SHUNT_R
#define INA219_SHUNT_R 0.1f
#endif

// Generic CJMCU-226 / GY-INA226 (R100), matching develop's setMaxCurrentShunt(0.8, 0.100).
#ifndef INA226_SHUNT_R
#define INA226_SHUNT_R 0.1f
#endif

// Adafruit 5832 / SparkFun Qwiic INA228 (15 mOhm), matching RobTillaart INA228 0.4.1.
#ifndef INA228_SHUNT_R
#define INA228_SHUNT_R 0.015f
#endif

// Generic INA3221 boards (100 mOhm), matching RobTillaart INA3221_RT 0.4.2.
#ifndef INA3221_SHUNT_R_CH1
#define INA3221_SHUNT_R_CH1 0.1f
#endif
#ifndef INA3221_SHUNT_R_CH2
#define INA3221_SHUNT_R_CH2 0.1f
#endif
#ifndef INA3221_SHUNT_R_CH3
#define INA3221_SHUNT_R_CH3 0.1f
#endif

#ifndef INA3221_ENV_CH
#define INA3221_ENV_CH 0
#endif
#ifndef INA3221_BAT_CH
#define INA3221_BAT_CH 0
#endif

// One power-monitor variant. Raw register words in, volts/mA out; no Wire.
struct InaChip {
    uint8_t busReg;
    uint8_t shuntReg;
    uint8_t bytes;
    uint8_t busShift;
    uint8_t shuntShift;
    bool directCurrent; // INA260: shuntReg is the current register
    float busLsb;       // V/LSB
    float shuntLsb;     // V/LSB, or A/LSB when directCurrent
    float currentMultiplier;
    uint8_t channels;
    bool environment; // report environment metrics
};

struct InaReading {
    float busV;
    float currentMa;
};

inline int32_t inaSignExtend(uint32_t value, uint8_t bits)
{
    uint32_t sign = 1u << (bits - 1);
    return (int32_t)((value ^ sign) - sign);
}

inline InaReading inaConvert(const InaChip &chip, uint32_t busRaw, uint32_t shuntRaw, float shuntR)
{
    InaReading r;
    r.busV = (float)(busRaw >> chip.busShift) * chip.busLsb;

    uint8_t shuntBits = chip.bytes * 8 - chip.shuntShift;
    int32_t shunt = inaSignExtend(shuntRaw >> chip.shuntShift, shuntBits);
    if (chip.directCurrent) {
        r.currentMa = (float)shunt * chip.shuntLsb * 1000.0f;
    } else {
        r.currentMa = shuntR > 0.0f ? (float)shunt * chip.shuntLsb / shuntR * 1000.0f : 0.0f;
    }
    return r;
}

inline float inaScaleCurrent(const InaChip &chip, float currentMa)
{
    return currentMa * chip.currentMultiplier;
}

// clang-format off
inline constexpr InaChip INA219_CHIP  = {0x02, 0x01, 2, 3, 0, false, 0.004f, 10e-6f, INA219_MULTIPLIER, 1, true};
inline constexpr InaChip INA226_CHIP  = {0x02, 0x01, 2, 0, 0, false, 0.00125f, 2.5e-6f, 1.0f, 1, false};
// INA260 has an internal 2 mOhm shunt; reg 0x01 is current in mA, so there is no shunt setting.
inline constexpr InaChip INA260_CHIP  = {0x02, 0x01, 2, 0, 0, true, 0.00125f, 0.00125f, 1.0f, 1, true};
inline constexpr InaChip INA228_CHIP  = {0x05, 0x04, 3, 4, 4, false, 195.3125e-6f, 312.5e-9f, 1.0f, 1, false};
inline constexpr InaChip INA3221_CHIP = {0x02, 0x01, 2, 3, 3, false, 0.008f, 40e-6f, 1.0f, 3, true};
// clang-format on
