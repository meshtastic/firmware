#include "CW2217.h"

bool CW2217::decode(const uint8_t regs[REG_COUNT], Reading &out)
{
    if (regs[REG_CHIP_ID] != CHIP_ID || regs[REG_MODE] != MODE_ACTIVE)
        return false;
    const uint32_t vcell = (regs[REG_VCELL] << 8) | regs[REG_VCELL + 1];
    const uint16_t mv = (uint16_t)(vcell * 5 / 16);
    if (mv < MIN_PLAUSIBLE_MV || mv > MAX_PLAUSIBLE_MV)
        return false;
    out.voltageMv = mv;
    // Rounded, not truncated: a full cell rests at 99 and three quarters.
    const uint32_t soc = (regs[REG_SOC] << 8) | regs[REG_SOC + 1];
    const uint32_t percent = (soc + 128) >> 8;
    out.percent = percent > 100 ? 100 : (uint8_t)percent;
    out.tempTenthsC = (int16_t)(regs[REG_TEMP] * 5 - 400);
    out.profileLoaded = regs[REG_SOC_ALERT] & 0x80;
    return true;
}
