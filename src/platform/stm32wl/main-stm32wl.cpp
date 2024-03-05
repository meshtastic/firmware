#include "RTC.h"
#include "configuration.h"
#include <stm32wle5xx.h>
#include <stm32wlxx_hal.h>

void setBluetoothEnable(bool enable) {}

void playStartMelody() {}

void updateBatteryLevel(uint8_t level) {}

void getMacAddr(uint8_t *dmac)
{
    // https://flit.github.io/2020/06/06/mcu-unique-id-survey.html
    const uint32_t uid0 = HAL_GetUIDw0(); // X/Y coordinate on wafer
    const uint32_t uid1 = HAL_GetUIDw1(); // [31:8] Lot number (23:0), [7:0] Wafer number
    const uint32_t uid2 = HAL_GetUIDw2(); // Lot number (55:24)

    // Need to go from 96-bit to 48-bit unique ID
    dmac[5] = (uint8_t)uid0;
    dmac[4] = (uint8_t)(uid0 >> 16);
    dmac[3] = (uint8_t)uid1;
    dmac[2] = (uint8_t)(uid1 >> 8);
    dmac[1] = (uint8_t)uid2;
    dmac[0] = (uint8_t)(uid2 >> 8);
}

void cpuDeepSleep(uint32_t msecToWake) {}

/* pacify libc_nano */
extern "C" {
int _gettimeofday(struct timeval *tv, void *tzvp)
{
    return -1;
}
}