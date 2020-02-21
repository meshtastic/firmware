#include "PeriodicTask.h"
#include "Periodic.h"

PeriodicTask::PeriodicTask(uint32_t initialPeriod) : period(initialPeriod)
{
}

/// call this from loop
void PeriodicTask::loop()
{
    uint32_t now = millis();
    if (period && (now - lastMsec) >= period)
    {
        lastMsec = now;
        doTask();
    }
}

void Periodic::doTask()
{
    uint32_t p = callback();
    setPeriod(p);
}