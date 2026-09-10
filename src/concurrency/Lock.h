#pragma once

#include "../freertosinc.h"

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

  private:
#ifdef HAS_FREE_RTOS
    SemaphoreHandle_t handle;
#endif
};

} // namespace concurrency
