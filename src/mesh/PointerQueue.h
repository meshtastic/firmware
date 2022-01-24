#pragma once

#include "TypedQueue.h"

/**
 * A wrapper for freertos queues that assumes each element is a pointer
 */
template <class T> class PointerQueue : public TypedQueue<T *>
{
  public:
    explicit PointerQueue(int maxElements) : TypedQueue<T *>(maxElements) {}

    // returns a ptr or null if the queue was empty
    T *dequeuePtr(TickType_t maxWait = portMAX_DELAY)
    {
        T *p;

        return this->dequeue(&p, maxWait) ? p : nullptr;
    }

#ifdef HAS_FREE_RTOS
    // returns a ptr or null if the queue was empty
    T *dequeuePtrFromISR(BaseType_t *higherPriWoken)
    {
        T *p;

        return this->dequeueFromISR(&p, higherPriWoken) ? p : nullptr;
    }
#endif
};
