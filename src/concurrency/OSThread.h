#pragma once

#include <cstdlib>
#include <stdint.h>

#include "Thread.h"
#include "ThreadController.h"

namespace concurrency
{

extern ThreadController mainController, timerController;

/**
 * @brief Base threading
 *
 * TODO FIXME @geeksville
 * basic functionality
 * sleeping the correct amount of time in main
 * NotifiedWorkerThread set/clears enabled
 *
 * notifyLater should start now - not relative to last start time
 * clear notification before calling handler
 *
 * stopping sleep instantly as soon as an event occurs.
 * use global functions delayTillWakeEvent(time), doWakeEvent(isInISR) - use freertos mutex or somesuch
 *
 * make everything use osthread
 *
 * Debug what is keeping us from sleeping
 *
 * have router thread block on its message queue in runOnce
 *
 * remove lock/lockguard
 */
class OSThread : public Thread
{
    ThreadController *controller;

  public:
    OSThread(const char *name, uint32_t period = 0, ThreadController *controller = &mainController);

    virtual ~OSThread();

    static void setup();

  protected:
    /**
     * The method that will be called each time our thread gets a chance to run
     *
     * Returns desired period for next invocation (or 0 for no change)
     */
    virtual uint32_t runOnce() = 0;

    // Do not override this
    virtual void run();
};

} // namespace concurrency
