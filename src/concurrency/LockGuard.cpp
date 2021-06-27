#include "configuration.h"
#include "LockGuard.h"

namespace concurrency {

LockGuard::LockGuard(Lock *lock) : lock(lock)
{
    lock->lock();
}

LockGuard::~LockGuard()
{
    lock->unlock();
}

} // namespace concurrency
