#include "concurrency/InterruptableDelay.h"
#include "configuration.h"

namespace concurrency
{

InterruptableDelay::InterruptableDelay()
{
    semaphore = xSemaphoreCreateBinary();
}

InterruptableDelay::~InterruptableDelay()
{
    vSemaphoreDelete(semaphore);
}

/**
 * Returns false if we were interrupted
 */
bool InterruptableDelay::delay(uint32_t msec)
{
    if (msec) {
        DEBUG_MSG("delay %u ", msec);

        // sem take will return false if we timed out (i.e. were not interrupted)
        bool r = xSemaphoreTake(semaphore, pdMS_TO_TICKS(msec));

        DEBUG_MSG("interrupt=%d\n", r);
        return !r;
    } else {
        return true;
    }
}

void InterruptableDelay::interrupt()
{
    xSemaphoreGive(semaphore);
}

IRAM_ATTR void InterruptableDelay::interruptFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
    xSemaphoreGiveFromISR(semaphore, pxHigherPriorityTaskWoken);
}

} // namespace concurrency