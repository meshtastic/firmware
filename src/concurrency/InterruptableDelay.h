#pragma once

#include "../freertosinc.h"

#ifdef HAS_FREE_RTOS
#include "concurrency/BinarySemaphoreFreeRTOS.h"
#define BinarySemaphore BinarySemaphoreFreeRTOS
#else
#include "concurrency/BinarySemaphorePosix.h"
#define BinarySemaphore BinarySemaphorePosix
#endif

namespace concurrency
{

/**
 * An object that provides delay(msec) like functionality, but can be interrupted by calling interrupt().
 *
 * Useful for they top level loop() delay call to keep the CPU powered down until our next scheduled event or some external event.
 *
 * This is implemented for FreeRTOS but should be easy to port to other operating systems.
 */
class InterruptableDelay
{
    BinarySemaphore semaphore;

  public:
    InterruptableDelay();
    ~InterruptableDelay();

    /**
     * Returns false if we were interrupted
     */
    bool delay(uint32_t msec);

    void interrupt();

    void interruptFromISR(BaseType_t *pxHigherPriorityTaskWoken);
};

} // namespace concurrency