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

// Hacks to force more code and data out.

// By default __assert_func uses fiprintf which pulls in stdio.
extern "C" void __wrap___assert_func(const char *, int, const char *, const char *)
{
    while (true)
        ;
    return;
}

// By default strerror has a lot of strings we probably don't use. Make it return an empty string instead.
char empty = 0;
extern "C" char *__wrap_strerror(int)
{
    return &empty;
}

#ifdef MESHTASTIC_EXCLUDE_TZ
struct _reent;

// Even if you don't use timezones, mktime will try to set the timezone anyway with _tzset_unlocked(), which pulls in scanf and
// friends. The timezone is initialized to UTC by default.
extern "C" void __wrap__tzset_unlocked_r(struct _reent *reent_ptr)
{
    return;
}
#endif