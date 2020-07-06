#include "Lock.h"
#include <cassert>

namespace concurrency {

Lock::Lock()
{
    handle = xSemaphoreCreateBinary();
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

} // namespace concurrency
