#pragma once

#include "Lock.h"
#include <cstdint>
#include <unordered_set>

namespace concurrency {

class PeriodicTask;

/**
 * @brief Runs all PeriodicTasks in the system. Currently called from main loop() 
 *        but eventually should be its own thread blocked on a freertos timer.
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
    Lock lock;

  public:
    /// Run any next tasks which are due for execution
    void loop();

  private:
    void schedule(PeriodicTask *t);
    void unschedule(PeriodicTask *t);
};

extern PeriodicScheduler periodicScheduler;

} // namespace concurrency
