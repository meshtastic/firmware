#include "configuration.h"
#include <pico/unique_id.h>
#include <stdio.h>

void setBluetoothEnable(bool on)
{
    // not needed
}

void cpuDeepSleep(uint32_t msecs)
{
    // not needed
}

void updateBatteryLevel(uint8_t level)
{
    // not needed
}

void getMacAddr(uint8_t *dmac)
{
    pico_unique_board_id_t src;
    pico_get_unique_board_id(&src);
    dmac[5] = src.id[7];
    dmac[4] = src.id[6];
    dmac[3] = src.id[5];
    dmac[2] = src.id[4];
    dmac[1] = src.id[3];
    dmac[0] = src.id[2];
}

void rp2040Setup()
{
    /* Sets a random seed to make sure we get different random numbers on each boot.
       Taken from CPU cycle counter and ROSC oscillator, so should be pretty random.
    */
    randomSeed(rp2040.hwrand32());
}

void enterDfuMode()
{
    reset_usb_boot(0, 0);
}