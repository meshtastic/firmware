#include "Lock.h"
#include "configuration.h"
#include <cassert>

namespace concurrency
{

#ifdef HAS_FREE_RTOS
Lock::Lock() : handle(xSemaphoreCreateBinary())
{
    assert(handle);
    if (xSemaphoreGive(handle) == false) {
        abort();
    }
}

void Lock::lock()
{
    if (xSemaphoreTake(handle, portMAX_DELAY) == false) {
        abort();
    }
}

void Lock::unlock()
{
    if (xSemaphoreGive(handle) == false) {
        abort();
    }
}
#else
Lock::Lock() {}

void Lock::lock() {}

void Lock::unlock() {}
#endif

} // namespace concurrency
