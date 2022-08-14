#include <stm32wle5xx.h>
#include <stm32wlxx_hal.h>
#include "configuration.h"
#include "RTC.h"

void setBluetoothEnable(bool on) {}

void playStartMelody() {}

void updateBatteryLevel(uint8_t level) {}

void getMacAddr(uint8_t *dmac)
{
    for (int i = 0; i < 6; i++)
        dmac[i] = i;
}

void cpuDeepSleep(uint64_t msecToWake) {}

/* pacify libc_nano */
extern "C" {
int _gettimeofday( struct timeval *tv, void *tzvp )
{
    return -1;
}
}

