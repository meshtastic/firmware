#pragma once

#include "../freertosinc.h"

namespace concurrency
{

#ifdef HAS_FREE_RTOS

class BinarySemaphoreFreeRTOS
{
    SemaphoreHandle_t semaphore;

  public:
    BinarySemaphoreFreeRTOS();
    ~BinarySemaphoreFreeRTOS();

    /**
     * Returns false if we timed out
     */
    bool take(uint32_t msec);

    void give();

    void giveFromISR(BaseType_t *pxHigherPriorityTaskWoken);
};

#endif

} // namespace concurrency