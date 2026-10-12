#include "Lock.h"
#include "configuration.h"
#include <cassert>

#if !defined(HAS_FREE_RTOS) && defined(ARCH_PORTDUINO)
#include <time.h>
#endif

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
#elif defined(ARCH_PORTDUINO)
Lock::Lock()
{
    // Same posture as the FreeRTOS branch above: a lock that cannot be created is not something a
    // caller can do anything about, and every use of an uninitialised mutex is undefined.
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        abort();
    }
}

void Lock::lock()
{
    pthread_mutex_lock(&mutex);
}

bool Lock::lock(uint32_t timeout)
{
    if (pthread_mutex_trylock(&mutex) == 0) {
        return true;
    }

    // pthread_mutex_timedlock is not portable (macOS has none), so poll to the deadline instead.
    // Returning true without acquiring is not an option: callers such as SPILock would unlock a
    // mutex they never took.
    struct timespec deadline = {};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += (long)(timeout % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    while (true) {
        struct timespec slice = {0, 200000L}; // 0.2 ms, fine enough for an SPI bus handover
        nanosleep(&slice, nullptr);

        // Checked immediately before the retry, so the lock is never taken past the deadline.
        struct timespec now = {};
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            return false;
        }

        if (pthread_mutex_trylock(&mutex) == 0) {
            return true;
        }
    }
}

void Lock::unlock()
{
    pthread_mutex_unlock(&mutex);
}

Lock::~Lock()
{
    pthread_mutex_destroy(&mutex);
}
#else
// Neither FreeRTOS nor pthreads: single-threaded targets such as STM32WL, whose newlib has no
// pthread at all. Unchanged from upstream - the real implementation above is Portduino's.
Lock::Lock() {}

Lock::~Lock() {}

void Lock::lock() {}

bool Lock::lock(uint32_t)
{
    return true;
}

void Lock::unlock() {}
#endif

} // namespace concurrency
