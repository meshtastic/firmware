#pragma once

#include "lock.h"
#include <cstdint>
#include <unordered_set>

class PeriodicTask;

/**
 * Runs all PeriodicTasks in the system.
 *
 * Currently called from main loop() but eventually should be its own thread blocked on a freertos timer.
 */
class PeriodicScheduler
{
    friend class PeriodicTask;

    /**
     * This really should be some form of heap, and when the period gets changed on a task it should get
     * rescheduled in that heap.  Currently it is just a dumb array and everytime we run loop() we check
     * _every_ tasks.  If it was a heap we'd only have to check the first task.
     */
    std::unordered_set<PeriodicTask *> tasks;

    // Protects the above variables.
    meshtastic::Lock lock;

  public:
    /// Run any next tasks which are due for execution
    void loop();

  private:
    void schedule(PeriodicTask *t);
    void unschedule(PeriodicTask *t);
};

extern PeriodicScheduler periodicScheduler;

/**
 * A base class for tasks that want their doTask() method invoked periodically
 *
 * FIXME: currently just syntatic sugar for polling in loop (you must call .loop), but eventually
 * generalize with the freertos scheduler so we can save lots of power by having everything either in
 * something like this or triggered off of an irq.
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

    /** MUST be be called once at startup (but after threading is running - i.e. not from a constructor)
     */
    void setup();

    /**
     * Set a new period in msecs (can be called from doTask or elsewhere and the scheduler will cope)
     * While zero this task is disabled and will not run
     */
    void setPeriod(uint32_t p) { period = p; }

    /**
     * Syntatic sugar for suspending tasks
     */
    void disable() { setPeriod(0); }

  protected:
    virtual void doTask() = 0;
};
