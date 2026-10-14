
#include "PowerHAL.h"

// PE/COFF has no ELF-style weak definitions, so a weak default is an undefined
// reference on Windows. Only nrf52 and nrf54l15 override these; define them strongly.
#ifdef _WIN32
#define POWERHAL_WEAK_DEFAULT __attribute__((noinline))
#else
#define POWERHAL_WEAK_DEFAULT __attribute__((weak, noinline))
#endif

void powerHAL_init()
{
    return powerHAL_platformInit();
}

POWERHAL_WEAK_DEFAULT void powerHAL_platformInit() {}

POWERHAL_WEAK_DEFAULT bool powerHAL_isPowerLevelSafe()
{
    return true;
}

POWERHAL_WEAK_DEFAULT bool powerHAL_isVBUSConnected()
{
    return false;
}
