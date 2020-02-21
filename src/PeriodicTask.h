#pragma once

#include <Arduino.h>
#include "configuration.h"

/**
 * A base class for tasks that want their doTask() method invoked periodically
 * 
 * FIXME: currently just syntatic sugar for polling in loop (you must call .loop), but eventually
 * generalize with the freertos scheduler so we can save lots of power by having everything either in
 * something like this or triggered off of an irq.
 */
class PeriodicTask
{
    uint32_t lastMsec = 0;
    uint32_t period = 1; // call soon after creation

public:
    uint32_t periodMsec;

    virtual ~PeriodicTask() {}

    PeriodicTask(uint32_t initialPeriod = 1);

    /// call this from loop
    virtual void loop();

    /// Set a new period in msecs (can be called from doTask or elsewhere and the scheduler will cope)
    void setPeriod(uint32_t p) { period = p; }

protected: 
    virtual void doTask() = 0;
};
