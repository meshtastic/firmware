#pragma once
#include <Arduino.h>

#define POOL_SIZE 16384

/**
 * An allocator (and placement new operator) that allocates storage from a fixed sized buffer.
 * It will panic if that buffer fills up.
 * If you are _sure_ no outstanding references to blocks in this buffer still exist, you can call
 * reset() to start from scratch.
 *
 * Currently the only usecase for this class is the ESP32 bluetooth stack, where once we've called deinit(false)
 * we are sure all those bluetooth objects no longer exist, and we'll need to recreate them when we restart bluetooth
 */
class SimpleAllocator
{
    uint8_t bytes[POOL_SIZE] = {};

    uint32_t nextFree = 0;

  public:
    SimpleAllocator();

    void *alloc(size_t size);

    /** If you are _sure_ no outstanding references to blocks in this buffer still exist, you can call
     * reset() to start from scratch.
     * */
    void reset();
};

void *operator new(size_t size, SimpleAllocator &p);

/**
 * Temporarily makes the specified Allocator be used for _all_ allocations.  Useful when calling library routines
 * that don't know about pools
 */
class AllocatorScope
{
  public:
    explicit AllocatorScope(SimpleAllocator &a);
    ~AllocatorScope();
};
