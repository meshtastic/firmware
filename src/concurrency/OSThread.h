#pragma once

#include <cstdlib>
#include <stdint.h>

#include "Thread.h"
#include "ThreadController.h"
#include "concurrency/InterruptableDelay.h"

namespace concurrency
{

extern ThreadController mainController, timerController;
extern InterruptableDelay mainDelay;

#define RUN_SAME -1

/**
 * @brief Base threading
 * 
 * This is a pseudo threading layer that is super easy to port, well suited to our slow network and very ram & power efficient.
 *
 * TODO FIXME @geeksville
 *
 * move more things into OSThreads
 * remove lock/lockguard
 * 
 * move typedQueue into concurrency
 * remove freertos from typedqueue
 */
class OSThread : public Thread
{
    ThreadController *controller;

    /// Show debugging info for disabled threads
    static bool showDisabled;

    /// Show debugging info for threads when we run them
    static bool showRun;

    /// Show debugging info for threads we decide not to run;
    static bool showWaiting;

  public:
    OSThread(const char *name, uint32_t period = 0, ThreadController *controller = &mainController);

    virtual ~OSThread();

    virtual bool shouldRun(unsigned long time);

    static void setup();

    /**
     * Wait a specified number msecs starting from the current time (rather than the last time we were run)
     */
    void setIntervalFromNow(unsigned long _interval);

  protected:
    /**
     * The method that will be called each time our thread gets a chance to run
     *
     * Returns desired period for next invocation (or RUN_SAME for no change)
     */
    virtual int32_t runOnce() = 0;

    // Do not override this
    virtual void run();
};

} // namespace concurrency
