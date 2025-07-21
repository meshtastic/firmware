/**
 * @file memGet.cpp
 * @brief Implementation of MemGet class that provides functions to get memory information.
 *
 * This file contains the implementation of MemGet class that provides functions to get
 * information about free heap, heap size, free psram and psram size. The functions are
 * implemented for ESP32 and NRF52 architectures. If the platform does not have heap
 * management function implemented, the functions return UINT32_MAX or 0.
 */
#include "memGet.h"
#include "configuration.h"

#ifdef ARCH_STM32WL
#include <malloc.h>
#endif

MemGet memGet;

/**
 * Returns the amount of free heap memory in bytes.
 * @return uint32_t The amount of free heap memory in bytes.
 */
uint32_t MemGet::getFreeHeap()
{
#ifdef ARCH_ESP32
    return ESP.getFreeHeap();
#elif defined(ARCH_NRF52)
    return dbgHeapFree();
#elif defined(ARCH_RP2040)
    return rp2040.getFreeHeap();
#elif defined(ARCH_STM32WL)
    struct mallinfo m = mallinfo();
    return m.fordblks; // Total free space (bytes)
#else
    // this platform does not have heap management function implemented
    return UINT32_MAX;
#endif
}

/**
 * Returns the size of the heap memory in bytes.
 * @return uint32_t The size of the heap memory in bytes.
 */
uint32_t MemGet::getHeapSize()
{
#ifdef ARCH_ESP32
    return ESP.getHeapSize();
#elif defined(ARCH_NRF52)
    return dbgHeapTotal();
#elif defined(ARCH_RP2040)
    return rp2040.getTotalHeap();
#elif defined(ARCH_STM32WL)
    struct mallinfo m = mallinfo();
    return m.arena; // Non-mmapped space allocated (bytes)
#else
    // this platform does not have heap management function implemented
    return UINT32_MAX;
#endif
}

/**
 * Returns the amount of free psram memory in bytes.
 *
 * @return The amount of free psram memory in bytes.
 */
uint32_t MemGet::getFreePsram()
{
#ifdef ARCH_ESP32
    return ESP.getFreePsram();
#elif defined(ARCH_PORTDUINO)
    return 4194252;
#else
    return 0;
#endif
}

/**
 * @brief Returns the size of the PSRAM memory.
 *
 * @return uint32_t The size of the PSRAM memory.
 */
uint32_t MemGet::getPsramSize()
{
#ifdef ARCH_ESP32
    return ESP.getPsramSize();
#elif defined(ARCH_PORTDUINO)
    return 4194252;
#else
    return 0;
#endif
}