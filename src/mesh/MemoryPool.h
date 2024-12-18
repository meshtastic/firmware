#pragma once

#include <Arduino.h>
#include <assert.h>

#include "PointerQueue.h"

template <class T> class Allocator
{

  public:
    virtual ~Allocator() {}

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
        T *p = alloc(maxWait);

        if (p)
            memset(p, 0, sizeof(T));
        return p;
    }

    /// Return a queable object which is a copy of some other object
    T *allocCopy(const T &src, TickType_t maxWait = portMAX_DELAY)
    {
        T *p = alloc(maxWait);
        assert(p);

        if (p)
            *p = src;
        return p;
    }

    /// Return a buffer for use by others
    virtual void release(T *p) = 0;

  protected:
    // Alloc some storage
    virtual T *alloc(TickType_t maxWait) = 0;
};

/**
 * An allocator that just uses regular free/malloc
 */
template <class T> class MemoryDynamic : public Allocator<T>
{
  public:
    /// Return a buffer for use by others
    virtual void release(T *p) override
    {
        assert(p);
        free(p);
    }

  protected:
    // Alloc some storage
    virtual T *alloc(TickType_t maxWait) override
    {
        T *p = (T *)malloc(sizeof(T));
        assert(p);
        return p;
    }
};
