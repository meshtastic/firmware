#pragma once

#include "../freertosinc.h"

#ifdef ARCH_PORTDUINO
#include <pthread.h>
#endif

namespace concurrency
{

#ifdef ARCH_PORTDUINO

class BinarySemaphorePosix
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;

  public:
    BinarySemaphorePosix();
    ~BinarySemaphorePosix();

    /**
     * Returns false if we timed out
     */
    bool take(uint32_t msec);

    void give();

    void giveFromISR(BaseType_t *pxHigherPriorityTaskWoken);
};

#endif

} // namespace concurrency
