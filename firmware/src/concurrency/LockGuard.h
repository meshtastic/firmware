#pragma once

#include "Lock.h"

namespace concurrency
{

/**
 * @brief RAII lock guard
 */
class LockGuard
{
  public:
    explicit LockGuard(Lock *lock);
    ~LockGuard();

    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

  private:
    Lock *lock;
};

} // namespace concurrency
