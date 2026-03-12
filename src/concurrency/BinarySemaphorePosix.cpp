#include "concurrency/BinarySemaphorePosix.h"
#include "configuration.h"

#include <errno.h>
#include <sys/time.h>

#ifndef HAS_FREE_RTOS

namespace concurrency
{

BinarySemaphorePosix::BinarySemaphorePosix()
{
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    signaled = false;
}

BinarySemaphorePosix::~BinarySemaphorePosix()
{
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
}

/**
 * Returns false if we timed out
 */
bool BinarySemaphorePosix::take(uint32_t msec)
{
    pthread_mutex_lock(&mutex);

    if (!signaled) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        ts.tv_sec += msec / 1000;
        ts.tv_nsec += (msec % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        while (!signaled) {
            int rc = pthread_cond_timedwait(&cond, &mutex, &ts);
            if (rc == ETIMEDOUT)
                break;
        }
    }

    bool wasSignaled = signaled;
    signaled = false; // consume the signal (binary semaphore)

    pthread_mutex_unlock(&mutex);
    return wasSignaled;
}

void BinarySemaphorePosix::give()
{
    pthread_mutex_lock(&mutex);
    signaled = true;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

IRAM_ATTR void BinarySemaphorePosix::giveFromISR(BaseType_t *pxHigherPriorityTaskWoken)
{
    give();
    if (pxHigherPriorityTaskWoken)
        *pxHigherPriorityTaskWoken = true;
}

} // namespace concurrency

#endif
