#include "PeriodicTask.h"
#include "Periodic.h"
#include "LockGuard.h"

namespace concurrency {

PeriodicScheduler periodicScheduler;

PeriodicTask::PeriodicTask(uint32_t initialPeriod) : period(initialPeriod) {}

void PeriodicTask::setup()
{
    periodicScheduler.schedule(this);
}

} // namespace concurrency
