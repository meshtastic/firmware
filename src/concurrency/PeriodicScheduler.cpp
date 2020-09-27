#include "PeriodicScheduler.h"
#include "PeriodicTask.h"
#include "LockGuard.h"

namespace concurrency {

/// call this from loop
void PeriodicScheduler::loop()
{
    LockGuard lg(&lock);

    uint32_t now = millis();
    for (auto t : tasks) {
        if (t->period && (now - t->lastMsec) >= t->period) {

            t->doTask();
            t->lastMsec = now;
        }
    }
}

void PeriodicScheduler::schedule(PeriodicTask *t)
{
    LockGuard lg(&lock);
    tasks.insert(t);
}

void PeriodicScheduler::unschedule(PeriodicTask *t)
{
    LockGuard lg(&lock);
    tasks.erase(t);
}

} // namespace concurrency
