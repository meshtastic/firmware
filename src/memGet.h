#pragma once
#ifndef _MT_MEMGET_H
#define _MT_MEMGET_H

#include <Arduino.h>

class MemGet
{
  public:
    uint32_t getFreeHeap();
    uint32_t getHeapSize();
    // Lowest free heap seen since boot, or 0 where the platform can't report it
    uint32_t getMinFreeHeap();
    // Largest block malloc() could still return, or 0 where the platform can't report it
    uint32_t getMaxAllocHeap();
    // False if the allocator's free list fails a sanity walk (heap corruption); true where it can't be checked
    bool heapLooksIntact();
    uint32_t getFreePsram();
    uint32_t getPsramSize();
};

extern MemGet memGet;

#endif
