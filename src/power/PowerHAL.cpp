
#include "PowerHAL.h"

void powerHAL_init()
{
    return powerHAL_platformInit();
}

__attribute__((weak, noinline)) void powerHAL_platformInit() {}

__attribute__((weak, noinline)) bool powerHAL_isPowerLevelSafe()
{
    return true;
}

__attribute__((weak, noinline)) bool powerHAL_isVBUSConnected()
{
    return false;
}
