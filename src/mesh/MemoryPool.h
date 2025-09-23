#pragma once

#include <Arduino.h>
#include <assert.h>
#include <functional>
#include <memory>

#include "PointerQueue.h"
#include "configuration.h" // For LOG_WARN, LOG_DEBUG, LOG_HEAP

template <class T> class Allocator
{

  public:
    Allocator() : deleter([this](T *p) { this->release(p); }) {}
    virtual ~Allocator() {}

    /// Return a queable object which has been prefilled with zeros.  Return nullptr if no buffer is available
    /// Note: this method is safe to call from regular OR ISR code
    T *allocZeroed()
    {
        T *p = allocZeroed(0);
        if (!p) {
            LOG_WARN("Failed to allocate zeroed memory");
        }
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
        if (!p) {
            LOG_WARN("Failed to allocate memory for copy");
            return nullptr;
        }

        *p = src;
        return p;
    }

    /// Variations of the above methods that return std::unique_ptr instead of raw pointers.
    using UniqueAllocation = std::unique_ptr<T, const std::function<void(T *)> &>;
    /// Return a queable object which has been prefilled with zeros.
    /// std::unique_ptr wrapped variant of allocZeroed().
    UniqueAllocation allocUniqueZeroed() { return UniqueAllocation(allocZeroed(), deleter); }
    /// Return a queable object which has been prefilled with zeros - allow timeout to wait for available buffers (you probably
    /// don't want this version).
    /// std::unique_ptr wrapped variant of allocZeroed(TickType_t maxWait).
    UniqueAllocation allocUniqueZeroed(TickType_t maxWait) { return UniqueAllocation(allocZeroed(maxWait), deleter); }
    /// Return a queable object which is a copy of some other object
    /// std::unique_ptr wrapped variant of allocCopy(const T &src, TickType_t maxWait).
    UniqueAllocation allocUniqueCopy(const T &src, TickType_t maxWait = portMAX_DELAY)
    {
        return UniqueAllocation(allocCopy(src, maxWait), deleter);
    }

    /// Return a buffer for use by others
    virtual void release(T *p) = 0;

  protected:
    // Alloc some storage
    virtual T *alloc(TickType_t maxWait) = 0;

  private:
    // std::unique_ptr Deleter function; calls release().
    const std::function<void(T *)> deleter;
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
        if (p == nullptr)
            return;

        LOG_HEAP("Freeing 0x%x", p);

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

/**
 * A static memory pool that uses a fixed buffer instead of heap allocation
 */
template <class T, int MaxSize> class MemoryPool : public Allocator<T>
{
  private:
    T pool[MaxSize];
    bool used[MaxSize];

  public:
    MemoryPool() : pool{}, used{}
    {
        // Arrays are now zero-initialized by member initializer list
        // pool array: all elements are default-constructed (zero for POD types)
        // used array: all elements are false (zero-initialized)
    }

    /// Return a buffer for use by others
    virtual void release(T *p) override
    {
        if (!p) {
            LOG_DEBUG("Failed to release memory, pointer is null");
            return;
        }

        // Find the index of this pointer in our pool
        int index = p - pool;
        if (index >= 0 && index < MaxSize) {
            assert(used[index]); // Should be marked as used
            used[index] = false;
            LOG_HEAP("Released static pool item %d at 0x%x", index, p);
        } else {
            LOG_WARN("Pointer 0x%x not from our pool!", p);
        }
    }

  protected:
    // Alloc some storage from our static pool
    virtual T *alloc(TickType_t maxWait) override
    {
        // Find first free slot
        for (int i = 0; i < MaxSize; i++) {
            if (!used[i]) {
                used[i] = true;
                LOG_HEAP("Allocated static pool item %d at 0x%x", i, &pool[i]);
                return &pool[i];
            }
        }

        // No free slots available - return nullptr instead of asserting
        LOG_WARN("No free slots available in static memory pool!");
        return nullptr;
    }
};
