#include "Lock.h"
#include "configuration.h"
#include <cassert>

namespace concurrency
{

#ifdef HAS_FREE_RTOS
Lock::Lock() : handle(xSemaphoreCreateBinary())
{
    assert(handle);
    assert(xSemaphoreGive(handle));
}

void Lock::lock()
{
    assert(xSemaphoreTake(handle, portMAX_DELAY));
}

void Lock::unlock()
{
    assert(xSemaphoreGive(handle));
}
#else
Lock::Lock() {}

void Lock::lock() {}

void Lock::unlock() {}
#endif

} // namespace concurrency
