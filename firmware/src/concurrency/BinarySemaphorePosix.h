#pragma once

#include "../freertosinc.h"

namespace concurrency
{

#ifndef HAS_FREE_RTOS

class BinarySemaphorePosix
{
    // SemaphoreHandle_t semaphore;

  public:
    BinarySemaphorePosix();
    ~BinarySemaphorePosix();

    /**
     * Returns false if we timed out
     */
    bool take(uint32_t msec);

    void give();

    void giveFromISR(BaseType_t *pxHigherPriorityTaskWoken);
};

#endif

} // namespace concurrency