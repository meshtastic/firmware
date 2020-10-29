#include "OSThread.h"
#include "configuration.h"
#include <assert.h>

namespace concurrency
{

/// Show debugging info for disabled threads
bool OSThread::showDisabled;

/// Show debugging info for threads when we run them
bool OSThread::showRun = false;

/// Show debugging info for threads we decide not to run;
bool OSThread::showWaiting = false;

ThreadController mainController, timerController;
InterruptableDelay mainDelay;

void OSThread::setup()
{
    mainController.ThreadName = "mainController";
    timerController.ThreadName = "timerController";
}

OSThread::OSThread(const char *_name, uint32_t period, ThreadController *_controller)
    : Thread(NULL, period), controller(_controller)
{
    ThreadName = _name;

    if (controller)
        controller->add(this);
}

OSThread::~OSThread()
{
    if (controller)
        controller->remove(this);
}

/**
 * Wait a specified number msecs starting from the current time (rather than the last time we were run)
 */
void OSThread::setIntervalFromNow(unsigned long _interval)
{
    // Save interval
    interval = _interval;

    // Cache the next run based on the last_run
    _cached_next_run = millis() + interval;
}

bool OSThread::shouldRun(unsigned long time)
{
    bool r = Thread::shouldRun(time);

    if (showRun && r)
        DEBUG_MSG("Thread %s: run\n", ThreadName.c_str());

    if (showWaiting && enabled && !r)
        DEBUG_MSG("Thread %s: wait %lu\n", ThreadName.c_str(), interval);

    if (showDisabled && !enabled)
        DEBUG_MSG("Thread %s: disabled\n", ThreadName.c_str());

    return r;
}

void OSThread::run()
{
    auto newDelay = runOnce();

    runned();

    if (newDelay >= 0)
        setInterval(newDelay);
}

} // namespace concurrency
