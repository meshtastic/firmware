#pragma once

#include "PeriodicScheduler.h"
#include "timing.h"

namespace concurrency {

/**
 * @brief A base class for tasks that want their doTask() method invoked periodically
 *
 * @todo currently just syntatic sugar for polling in loop (you must call .loop), but eventually
 *        generalize with the freertos scheduler so we can save lots of power by having everything either in
 *        something like this or triggered off of an irq.
 */
class PeriodicTask
{
    friend class PeriodicScheduler;

    uint32_t lastMsec = 0;
    uint32_t period = 1; // call soon after creation

  public:
    virtual ~PeriodicTask() { periodicScheduler.unschedule(this); }

    /**
     * Constructor (will schedule with the global PeriodicScheduler)
     */
    PeriodicTask(uint32_t initialPeriod = 1);

    /** 
     * MUST be be called once at startup (but after threading is running - i.e. not from a constructor)
     */
    void setup();

    /**
     * Set a new period in msecs (can be called from doTask or elsewhere and the scheduler will cope)
     * While zero this task is disabled and will not run
     */
    void setPeriod(uint32_t p)
    {
        lastMsec = timing::millis(); // reset starting from now
        period = p;
    }

    uint32_t getPeriod() const { return period; }

    /**
     * Syntatic sugar for suspending tasks
     */
    void disable() { setPeriod(0); }

  protected:
    virtual void doTask() = 0;
};

} // namespace concurrency
