#include "Lock.h"
#include "configuration.h"
#include <cassert>
#include <chrono>
#include <thread>
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

bool Lock::lock(uint32_t timeout)
{
    return xSemaphoreTake(handle, pdMS_TO_TICKS(timeout)) == pdTRUE;
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

bool Lock::lock(uint32_t timeout)
{
    // pthread_mutex_timedlock is absent on macOS, so poll trylock instead.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    do {
        if (pthread_mutex_trylock(&mutex) == 0) {
            locked = true;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

Lock::~Lock()
{
    pthread_mutex_destroy(&mutex);
}
#endif

} // namespace concurrency
