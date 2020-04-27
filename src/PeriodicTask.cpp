#include "PeriodicTask.h"
#include "Periodic.h"
PeriodicScheduler periodicScheduler;

PeriodicTask::PeriodicTask(uint32_t initialPeriod) : period(initialPeriod) {}

void PeriodicTask::setup()
{
    periodicScheduler.schedule(this);
}

/// call this from loop
void PeriodicScheduler::loop()
{
    meshtastic::LockGuard lg(&lock);

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
    meshtastic::LockGuard lg(&lock);
    tasks.insert(t);
}

void PeriodicScheduler::unschedule(PeriodicTask *t)
{
    meshtastic::LockGuard lg(&lock);
    tasks.erase(t);
}

void Periodic::doTask()
{
    uint32_t p = callback();
    setPeriod(p);
}
