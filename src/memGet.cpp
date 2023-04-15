#include "memGet.h"
#include "configuration.h"

MemGet memGet;

uint32_t MemGet::getFreeHeap()
{
#ifdef ARCH_ESP32
    return ESP.getFreeHeap();
#elif defined(ARCH_NRF52)
    return dbgHeapFree();
#else
    // this platform does not have heap management function implemented
    return UINT32_MAX;
#endif
}

uint32_t MemGet::getHeapSize()
{
#ifdef ARCH_ESP32
    return ESP.getHeapSize();
#elif defined(ARCH_NRF52)
    return dbgHeapTotal();
#else
    // this platform does not have heap management function implemented
    return UINT32_MAX;
#endif
}

uint32_t MemGet::getFreePsram()
{
#ifdef ARCH_ESP32
    return ESP.getFreePsram();
#else
    return 0;
#endif
}

uint32_t MemGet::getPsramSize()
{
#ifdef ARCH_ESP32
    return ESP.getPsramSize();
#else
    return 0;
#endif
}
