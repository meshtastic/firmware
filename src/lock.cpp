#include "lock.h"

#include <cassert>

namespace meshtastic
{

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

LockGuard::LockGuard(Lock *lock) : lock(lock)
{
    lock->lock();
}

LockGuard::~LockGuard()
{
    lock->unlock();
}

} // namespace meshtastic
