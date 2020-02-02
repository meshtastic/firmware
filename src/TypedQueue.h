#pragma once 

#include <Arduino.h>
#include <assert.h>

/**
 * A wrapper for freertos queues.  Note: each element object must be quite small, so T should be only
 * pointer types or ints
 */
template <class T> class TypedQueue {
    QueueHandle_t h;
public:
    TypedQueue(int maxElements) {
        h = xQueueCreate(maxElements, sizeof(T));
        assert(h);
    }

    ~TypedQueue() {
        vQueueDelete(h);
    }

    // pdTRUE for success else failure
    BaseType_t enqueue(T x, TickType_t maxWait = portMAX_DELAY) {
        return xQueueSendToBack(h, &x, maxWait);
    }

    // pdTRUE for success else failure
    BaseType_t dequeue(T *p, TickType_t maxWait = portMAX_DELAY) {
        return xQueueReceive(h, p, maxWait);
    }
};


/**
 * A wrapper for freertos queues that assumes each element is a pointer
 */
template <class T> class PointerQueue: public TypedQueue<T *> {
    TypedQueue h;
public:
    PointerQueue(int maxElements) : TypedQueue(maxElements) {
    }

    // preturns a ptr or null if the queue was empty
    T *dequeuePtr(TickType_t maxWait = portMAX_DELAY) {
        T *p;
        
        return dequeue(&p, maxWait)  == pdTRUE ? p : NULL;
    }
};

