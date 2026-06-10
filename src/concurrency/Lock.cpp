#include "Lock.h"
#include "configuration.h"
#include <cassert>
#include <logging.h>

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

Lock::~Lock()
{
    vSemaphoreDelete(handle);
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
Lock::Lock()
{
    pthread_mutex_init(&mutex, NULL);
}

void Lock::lock()
{
    if (locked) {
        LOG_INFO("Attempt to lock an already locked Lock!");
    }
    pthread_mutex_lock(&mutex);
    locked = true;

    if (console)
        LOG_WARN("Lock");
}

void Lock::unlock()
{
    pthread_mutex_unlock(&mutex);
    locked = false;
}

Lock::~Lock()
{
    pthread_mutex_destroy(&mutex);
}
#endif

} // namespace concurrency
