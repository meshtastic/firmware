#include "concurrency/InterruptableDelay.h"
#include "configuration.h"

namespace concurrency
{

InterruptableDelay::InterruptableDelay() {}

InterruptableDelay::~InterruptableDelay() {}

/**
 * Returns false if we were interrupted
 */
bool InterruptableDelay::delay(uint32_t msec)
{
    // LOG_DEBUG("delay %u ", msec);

    // sem take will return false if we timed out (i.e. were not interrupted)
    bool r = semaphore.take(msec);

    // LOG_DEBUG("interrupt=%d\n", r);
    return !r;
}

void InterruptableDelay::interrupt()
{
    semaphore.give();
}

IRAM_ATTR void InterruptableDelay::interruptFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
    semaphore.giveFromISR(pxHigherPriorityTaskWoken);
}

} // namespace concurrency