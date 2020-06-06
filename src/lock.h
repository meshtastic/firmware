#pragma once

#include "freertosinc.h"

namespace meshtastic
{

// Simple wrapper around FreeRTOS API for implementing a mutex lock.
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
#ifdef configUSE_PREEMPTION
    SemaphoreHandle_t handle;
#endif
};

// RAII lock guard.
class LockGuard
{
  public:
    LockGuard(Lock *lock);
    ~LockGuard();

    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

  private:
    Lock *lock;
};

} // namespace meshtastic
