#pragma once

#include <stdint.h>

// Cellwise CW2217 fuel gauge: decoding of its registers 0x00-0x0F, read in one burst.
// The battery profile is loaded at the factory; this side only reads.
namespace CW2217
{
constexpr uint8_t CHIP_ID = 0xA0;
constexpr uint8_t REG_COUNT = 16;
constexpr uint8_t REG_CHIP_ID = 0x00;
constexpr uint8_t REG_VCELL = 0x02;     // 16 bits, 5/16 mV per step
constexpr uint8_t REG_SOC = 0x04;       // integer percent, then 1/256 percent
constexpr uint8_t REG_TEMP = 0x06;      // 0.5 C per step from -40 C
constexpr uint8_t REG_MODE = 0x08;      // 0x00 when the gauge is running
constexpr uint8_t REG_SOC_ALERT = 0x0B; // bit 7 set once a battery profile has been loaded
constexpr uint8_t REG_IC_STATE = 0xA7;
constexpr uint8_t MODE_ACTIVE = 0x00;
constexpr uint16_t MIN_PLAUSIBLE_MV = 2500;
constexpr uint16_t MAX_PLAUSIBLE_MV = 4700;

struct Reading {
    uint16_t voltageMv = 0;
    uint8_t percent = 0;
    int16_t tempTenthsC = 0;
    bool profileLoaded = false;
};

// False unless regs come from a running CW2217 with a plausible cell voltage.
bool decode(const uint8_t regs[REG_COUNT], Reading &out);
} // namespace CW2217
