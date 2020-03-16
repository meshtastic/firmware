#include "PeriodicTask.h"
#include "Periodic.h"

PeriodicTask::PeriodicTask(uint32_t initialPeriod) : period(initialPeriod) {}

/// call this from loop
void PeriodicTask::loop()
{
    {
        meshtastic::LockGuard lg(&lock);
        uint32_t now = millis();
        if (!period || (now - lastMsec) < period) {
            return;
        }
        lastMsec = now;
    }
    // Release the lock in case the task wants to change the period.
    doTask();
}

void Periodic::doTask()
{
    uint32_t p = callback();
    setPeriod(p);
}
