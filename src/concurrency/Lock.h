#pragma once

#include "../freertosinc.h"

#if !defined(HAS_FREE_RTOS) && defined(ARCH_PORTDUINO)
#include <pthread.h>
#endif

namespace concurrency
{

/**
 * @brief Simple wrapper around FreeRTOS API for implementing a mutex lock
 */
class Lock
{
  public:
    Lock();
    ~Lock();

    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

    /// Locks the lock.
    //
    // Must not be called from an ISR.
    void lock();

    /// Locks the lock with timeout.
    //
    // Must not be called from an ISR.
    bool lock(uint32_t timeout);

    // Unlocks the lock.
    //
    // Must not be called from an ISR.
    void unlock();

#ifdef HAS_FREE_RTOS
    /// From an ISR: take the lock only if it is free now. Never blocks.
    bool tryLockFromISR();

    /// From an ISR: release a lock taken with tryLockFromISR().
    void unlockFromISR();
#endif

  private:
#ifdef HAS_FREE_RTOS
    SemaphoreHandle_t handle;
#elif defined(ARCH_PORTDUINO)
    pthread_mutex_t mutex;
#endif
};

} // namespace concurrency
