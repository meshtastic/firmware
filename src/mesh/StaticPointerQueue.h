#pragma once

#include "concurrency/OSThread.h"
#include "freertosinc.h"
#include <cassert>

/**
 * A static circular buffer queue for pointers.
 * This provides the same interface as PointerQueue but uses a statically allocated
 * buffer instead of dynamic allocation.
 */
template <class T, int MaxElements> class StaticPointerQueue
{
    static_assert(MaxElements > 0, "MaxElements must be greater than 0");

    T *buffer[MaxElements];
    int head = 0;
    int tail = 0;
    int count = 0;
    concurrency::OSThread *reader = nullptr;

  public:
    StaticPointerQueue()
    {
        // Initialize all buffer elements to nullptr to silence warnings and ensure clean state
        for (int i = 0; i < MaxElements; i++) {
            buffer[i] = nullptr;
        }
    }

    int numFree() const { return MaxElements - count; }

    bool isEmpty() const { return count == 0; }

    int numUsed() const { return count; }

    bool enqueue(T *x, TickType_t maxWait = portMAX_DELAY)
    {
        if (count >= MaxElements) {
            return false; // Queue is full
        }

        if (reader) {
            reader->setInterval(0);
            concurrency::mainDelay.interrupt();
        }

        buffer[tail] = x;
        tail = (tail + 1) % MaxElements;
        count++;
        return true;
    }

    bool dequeue(T **p, TickType_t maxWait = portMAX_DELAY)
    {
        if (count == 0) {
            return false; // Queue is empty
        }

        *p = buffer[head];
        head = (head + 1) % MaxElements;
        count--;
        return true;
    }

    // returns a ptr or null if the queue was empty
    T *dequeuePtr(TickType_t maxWait = portMAX_DELAY)
    {
        T *p;
        return dequeue(&p, maxWait) ? p : nullptr;
    }

    void setReader(concurrency::OSThread *t) { reader = t; }

    // For compatibility with PointerQueue interface
    int getMaxLen() const { return MaxElements; }
};
