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

#if defined(MESHTASTIC_DYNAMIC_SBRK_HEAP)
#include <malloc.h>
#include <unistd.h> // sbrk

// Returns the uncommitted sbrk headroom: addressable space between the current heap
// break and the stack pointer that has not yet been committed to the arena.
// Currently used on: ARCH_STM32WL
static uint32_t sbrkHeadroom()
{
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    uint32_t heap_end = (uint32_t)sbrk(0);
    return (sp > heap_end) ? (sp - heap_end) : 0;
}
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
#elif defined(MESHTASTIC_DYNAMIC_SBRK_HEAP) // Currently: ARCH_STM32WL
    struct mallinfo m = mallinfo();
    return m.fordblks + sbrkHeadroom(); // Free space within arena + uncommitted sbrk headroom
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
#elif defined(MESHTASTIC_DYNAMIC_SBRK_HEAP) // Currently: ARCH_STM32WL
    struct mallinfo m = mallinfo();
    return m.arena + sbrkHeadroom(); // Non-mmapped space allocated + uncommitted sbrk headroom
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

void displayPercentHeapFree()
{
    uint32_t freeHeap = memGet.getFreeHeap();
    uint32_t totalHeap = memGet.getHeapSize();
    if (totalHeap == 0 || totalHeap == UINT32_MAX) {
        LOG_INFO("Heap size unavailable");
        return;
    }
    int percent = (int)((freeHeap * 100) / totalHeap);
    LOG_INFO("Heap free: %d%% (%u/%u bytes)", percent, freeHeap, totalHeap);
}