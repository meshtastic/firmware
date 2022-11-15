#include "configuration.h"
#include <stdio.h>
#include <pico/unique_id.h>

void setBluetoothEnable(bool on)
{
    // not needed
}

void cpuDeepSleep(uint64_t msecs)
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
    dmac[5] = src.id[0];
    dmac[4] = src.id[1];
    dmac[3] = src.id[2];
    dmac[2] = src.id[3];
    dmac[1] = src.id[4];
    dmac[0] = src.id[5];
}
