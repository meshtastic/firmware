#pragma once 

#include <Arduino.h>
#include <assert.h>


/**
 * A pool based allocator
 * 
 * Eventually this routine will even be safe for ISR use...
 */
template <class T> class MemoryPool {
    TypedQueue<T *> dead;

    T *buf; // our large raw block of memory

public:
    MemoryPool(int maxElements): queued(maxElements), dead(maxElements) {
        buf = new T[maxElements];
        
        // prefill dead
        for(int i = 0; i < maxElements; i++)
            release(&buf[i]);
    }

    ~MemoryPool() {
        delete[] buf;
    }

    /// Return a queable object which has been prefilled with zeros
    T *allocZeroed(TickType_t maxWait = portMAX_DELAY) {
        T *p;

        if(dead.dequeue(&p, maxWait) != pdTRUE)
            return NULL;

        memset(p, 0, sizeof(T));
        return p;
    }

    /// Return a buffer for use by others
    void free(T *p) {
        int res = dead.enqueue(p, 0);
        assert(res == pdTRUE);
    }
};

