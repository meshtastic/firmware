#include "concurrency/BinarySemaphorePosix.h"
#include "configuration.h"

#ifndef HAS_FREE_RTOS

namespace concurrency
{

BinarySemaphorePosix::BinarySemaphorePosix() {}

BinarySemaphorePosix::~BinarySemaphorePosix() {}

/**
 * Returns false if we timed out
 */
bool BinarySemaphorePosix::take(uint32_t msec)
{
    delay(msec); // FIXME
    return false;
}

void BinarySemaphorePosix::give() {}

IRAM_ATTR void BinarySemaphorePosix::giveFromISR(BaseType_t *pxHigherPriorityTaskWoken) {}

} // namespace concurrency

#endif