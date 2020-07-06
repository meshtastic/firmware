#pragma once

#include "../freertosinc.h"

namespace concurrency {

/**
 * @brief Simple wrapper around FreeRTOS API for implementing a mutex lock
 */
class Lock
{
  public:
    Lock();

    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

    /// Locks the lock.
    //
    // Must not be called from an ISR.
    void lock();

    // Unlocks the lock.
    //
    // Must not be called from an ISR.
    void unlock();

  private:
    SemaphoreHandle_t handle;

};

} // namespace concurrency
