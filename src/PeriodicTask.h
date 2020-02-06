#pragma once

#include <Arduino.h>
#include "configuration.h"

class PeriodicTask
{
    /// we use prevMsec rather than nextMsec because it is easier to handle the uint32 rollover in that case, also changes in periodMsec take effect immediately
    uint32_t prevMsec;

public:
    uint32_t periodMsec;

    virtual ~PeriodicTask() {}

    PeriodicTask(uint32_t period) : periodMsec(period)
    {
        prevMsec = millis();
    }

    /// call this from loop
    virtual void loop()
    {
        uint32_t now = millis();
        if (now > (prevMsec + periodMsec))
        {
            // FIXME, this lets period slightly drift based on scheduling - not sure if that is always good
            prevMsec = now;

            // DEBUG_MSG("Calling periodic task\n");
            doTask();
        }
    }

    virtual void doTask() = 0;
};
