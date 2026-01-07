#include "concurrency/BinarySemaphoreFreeRTOS.h"
#include "configuration.h"
#include <assert.h>

#ifdef HAS_FREE_RTOS

namespace concurrency
{

BinarySemaphoreFreeRTOS::BinarySemaphoreFreeRTOS() : semaphore(xSemaphoreCreateBinary())
{
    assert(semaphore);
}

BinarySemaphoreFreeRTOS::~BinarySemaphoreFreeRTOS()
{
    vSemaphoreDelete(semaphore);
}

/**
 * Returns false if we were interrupted
 */
bool BinarySemaphoreFreeRTOS::take(uint32_t msec)
{
    return xSemaphoreTake(semaphore, pdMS_TO_TICKS(msec));
}

void BinarySemaphoreFreeRTOS::give()
{
    xSemaphoreGive(semaphore);
}

IRAM_ATTR void BinarySemaphoreFreeRTOS::giveFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
    xSemaphoreGiveFromISR(semaphore, pxHigherPriorityTaskWoken);
}

} // namespace concurrency

#endif
