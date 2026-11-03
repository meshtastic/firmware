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

#if defined(ARCH_STM32)
// Returns the uncommitted sbrk headroom: addressable space between the current heap
// break and the stack pointer that has not yet been committed to the arena.
static uint32_t sbrkHeadroom()
{
    // defined in STM32 linker script
    extern char _estack;
    extern char _Min_Stack_Size;

    // Both are linker-script symbols rather than real objects: _estack is the top-of-RAM address and
    // _Min_Stack_Size is a plain value. Convert each to an integer before subtracting - differencing the
    // pointers themselves is pointer arithmetic across unrelated objects.
    uint32_t max_sp = (uint32_t)&_estack - (uint32_t)&_Min_Stack_Size;
    uint32_t heap_end = (uint32_t)sbrk(0);
    return (max_sp > heap_end) ? (max_sp - heap_end) : 0;
}
#else
#error Unsupported architecture!
#endif
#endif

MemGet memGet;

#ifdef ARCH_NRF52
#include <unistd.h> // sbrk

extern "C" char __HeapBase[], __HeapLimit[];
// newlib-nano's free list: each chunk starts with its size (header included) and the next free chunk
struct NanoFreeChunk {
    long size;
    NanoFreeChunk *next;
};
extern "C" NanoFreeChunk *__malloc_free_list;

/// Walk newlib-nano's free list for the largest free chunk; false if a link leaves the heap or the list doesn't end.
static bool walkNrf52FreeList(uint32_t &largest)
{
    largest = 0;
    bool intact = true;
    uint32_t chunks = 0;
    vTaskSuspendAll();
    for (const NanoFreeChunk *c = __malloc_free_list; c; c = c->next) {
        const char *p = reinterpret_cast<const char *>(c);
        if (p < __HeapBase || p >= __HeapLimit || (reinterpret_cast<uintptr_t>(p) & 3) || c->size <= 0 ||
            c->size > __HeapLimit - p || ++chunks > 2048) {
            intact = false;
            break;
        }
        if ((uint32_t)c->size > largest)
            largest = c->size;
    }
    const char *brk = static_cast<const char *>(sbrk(0));
    (void)xTaskResumeAll();
    // The never-claimed space above the break is one more block
    if (brk >= __HeapBase && brk <= __HeapLimit && (uint32_t)(__HeapLimit - brk) > largest)
        largest = __HeapLimit - brk;
    return intact;
}
#endif

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
 * Returns the lowest the free heap has ever been since boot.
 * @return uint32_t Low watermark in bytes, or 0 if the platform can't report it.
 */
uint32_t MemGet::getMinFreeHeap()
{
#ifdef ARCH_ESP32
    return ESP.getMinFreeHeap();
#else
    return 0;
#endif
}

/**
 * Returns the largest contiguous block malloc() could still return.
 * @return uint32_t Block size in bytes, or 0 if the platform can't report it.
 */
uint32_t MemGet::getMaxAllocHeap()
{
#ifdef ARCH_ESP32
    return ESP.getMaxAllocHeap();
#elif defined(ARCH_NRF52)
    uint32_t largest;
    walkNrf52FreeList(largest);
    return largest;
#else
    return 0;
#endif
}

/**
 * Sanity-walks the allocator's free list, where the platform allows it.
 * @return false if the list looks corrupt, true if it is intact or can't be checked.
 */
bool MemGet::heapLooksIntact()
{
#ifdef ARCH_NRF52
    uint32_t largest;
    return walkNrf52FreeList(largest);
#else
    return true;
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
