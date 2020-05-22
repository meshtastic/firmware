#pragma once

#include <Arduino.h>
#include <assert.h>

#include "PointerQueue.h"

/**
 * A pool based allocator
 *
 * Eventually this routine will even be safe for ISR use...
 */
template <class T> class MemoryPool
{
    PointerQueue<T> dead;

    T *buf; // our large raw block of memory

    size_t maxElements;

  public:
    MemoryPool(size_t _maxElements) : dead(_maxElements), maxElements(_maxElements)
    {
        buf = new T[maxElements];

        // prefill dead
        for (size_t i = 0; i < maxElements; i++)
            release(&buf[i]);
    }

    ~MemoryPool() { delete[] buf; }

    /// Return a queable object which has been prefilled with zeros.  Panic if no buffer is available
    /// Note: this method is safe to call from regular OR ISR code
    T *allocZeroed()
    {
        T *p = allocZeroed(0);

        assert(p); // FIXME panic instead
        return p;
    }

    /// Return a queable object which has been prefilled with zeros - allow timeout to wait for available buffers (you probably
    /// don't want this version).
    T *allocZeroed(TickType_t maxWait)
    {
        T *p = dead.dequeuePtr(maxWait);

        if (p)
            memset(p, 0, sizeof(T));
        return p;
    }

    /// Return a queable object which is a copy of some other object
    T *allocCopy(const T &src, TickType_t maxWait = portMAX_DELAY)
    {
        T *p = dead.dequeuePtr(maxWait);

        if (p)
            *p = src;
        return p;
    }

    /// Return a buffer for use by others
    void release(T *p)
    {
        assert(dead.enqueue(p, 0));
        assert(p >= buf &&
               (size_t)(p - buf) <
                   maxElements); // sanity check to make sure a programmer didn't free something that didn't come from this pool
    }

    /// Return a buffer from an ISR, if higherPriWoken is set to true you have some work to do ;-)
    void releaseFromISR(T *p, BaseType_t *higherPriWoken)
    {
        assert(dead.enqueueFromISR(p, higherPriWoken));
        assert(p >= buf &&
               (size_t)(p - buf) <
                   maxElements); // sanity check to make sure a programmer didn't free something that didn't come from this pool
    }
};
