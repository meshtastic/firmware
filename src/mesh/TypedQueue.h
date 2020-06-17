#pragma once

#include <cassert>
#include <type_traits>

#include "freertosinc.h"

#ifdef HAS_FREE_RTOS

/**
 * A wrapper for freertos queues.  Note: each element object should be small
 * and POD (Plain Old Data type) as elements are memcpied by value.
 */
template <class T> class TypedQueue
{
    static_assert(std::is_pod<T>::value, "T must be pod");
    QueueHandle_t h;

  public:
    TypedQueue(int maxElements)
    {
        h = xQueueCreate(maxElements, sizeof(T));
        assert(h);
    }

    ~TypedQueue() { vQueueDelete(h); }

    int numFree() { return uxQueueSpacesAvailable(h); }

    bool isEmpty() { return uxQueueMessagesWaiting(h) == 0; }

    bool enqueue(T x, TickType_t maxWait = portMAX_DELAY) { return xQueueSendToBack(h, &x, maxWait) == pdTRUE; }

    bool enqueueFromISR(T x, BaseType_t *higherPriWoken) { return xQueueSendToBackFromISR(h, &x, higherPriWoken) == pdTRUE; }

    bool dequeue(T *p, TickType_t maxWait = portMAX_DELAY) { return xQueueReceive(h, p, maxWait) == pdTRUE; }

    bool dequeueFromISR(T *p, BaseType_t *higherPriWoken) { return xQueueReceiveFromISR(h, p, higherPriWoken); }
};

#else

#include <queue>

/**
 * A wrapper for freertos queues.  Note: each element object should be small
 * and POD (Plain Old Data type) as elements are memcpied by value.
 */
template <class T> class TypedQueue
{
    std::queue<T> q;

  public:
    TypedQueue(int maxElements) {}

    // int numFree() { return uxQueueSpacesAvailable(h); }

    bool isEmpty() { return q.empty(); }

    bool enqueue(T x, TickType_t maxWait = portMAX_DELAY)
    {
        q.push(x);
        return true;
    }

    // bool enqueueFromISR(T x, BaseType_t *higherPriWoken) { return xQueueSendToBackFromISR(h, &x, higherPriWoken) == pdTRUE; }

    bool dequeue(T *p, TickType_t maxWait = portMAX_DELAY)
    {
        if (isEmpty())
            return false;
        else {
            *p = q.front();
            q.pop();
            return true;
        }
    }

    // bool dequeueFromISR(T *p, BaseType_t *higherPriWoken) { return xQueueReceiveFromISR(h, p, higherPriWoken); }
};
#endif
