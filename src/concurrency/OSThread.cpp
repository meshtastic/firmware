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

const OSThread *OSThread::currentThread;

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
    assertIsSetup();

    ThreadName = _name;

    if (controller) {
        bool added = controller->add(this);
        assert(added);
    }
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
    currentThread = this;
    auto newDelay = runOnce();

    runned();

    if (newDelay >= 0)
        setInterval(newDelay);

    currentThread = NULL;
}

/**
 * This flag is set **only** when setup() starts, to provide a way for us to check for sloppy static constructor calls.
 * Call assertIsSetup() to force a crash if someone tries to create an instance too early.
 *
 * it is super important to never allocate those object statically.  instead, you should explicitly
 *  new them at a point where you are guaranteed that other objects that this instance
 * depends on have already been created.
 *
 * in particular, for OSThread that means "all instances must be declared via new() in setup() or later" -
 * this makes it guaranteed that the global mainController is fully constructed first.
 */
bool hasBeenSetup;

void assertIsSetup()
{

    /**
     * Dear developer comrade - If this assert fails() that means you need to fix the following:
     *
     * This flag is set **only** when setup() starts, to provide a way for us to check for sloppy static constructor calls.
     * Call assertIsSetup() to force a crash if someone tries to create an instance too early.
     *
     * it is super important to never allocate those object statically.  instead, you should explicitly
     *  new them at a point where you are guaranteed that other objects that this instance
     * depends on have already been created.
     *
     * in particular, for OSThread that means "all instances must be declared via new() in setup() or later" -
     * this makes it guaranteed that the global mainController is fully constructed first.
     */
    assert(hasBeenSetup);
}

} // namespace concurrency
