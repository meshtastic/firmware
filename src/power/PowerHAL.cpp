
#include "PowerHAL.h"

// PE/COFF has no ELF-style weak definitions: GNU as lowers __attribute__((weak))
// to a COFF weak external, which the linker treats as an undefined reference
// rather than a fallback, so these defaults would not link on Windows. Only
// nrf52 and nrf54l15 override them, so define them strongly there.
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
