#include "NotifiedWorkerThread.h"
#include "configuration.h"
#include "main.h"

namespace concurrency
{

static bool debugNotification;

/**
 * Notify this thread so it can run
 */
bool NotifiedWorkerThread::notify(uint32_t v, bool overwrite)
{
    bool r = notifyCommon(v, overwrite);

    if (r)
        mainDelay.interrupt();

    return r;
}

/**
 * Notify this thread so it can run
 */
IRAM_ATTR bool NotifiedWorkerThread::notifyCommon(uint32_t v, bool overwrite)
{
    if (overwrite || notification == 0) {
        enabled = true;
        setInterval(0); // Run ASAP
        runASAP = true;

        notification = v;
        if (debugNotification) {
            LOG_DEBUG("setting notification %d\n", v);
        }
        return true;
    } else {
        if (debugNotification) {
            LOG_DEBUG("dropping notification %d\n", v);
        }
        return false;
    }
}

/**
 * Notify from an ISR
 *
 * This must be inline or IRAM_ATTR on ESP32
 */
IRAM_ATTR bool NotifiedWorkerThread::notifyFromISR(BaseType_t *highPriWoken, uint32_t v, bool overwrite)
{
    bool r = notifyCommon(v, overwrite);
    if (r)
        mainDelay.interruptFromISR(highPriWoken);

    return r;
}

/**
 * Schedule a notification to fire in delay msecs
 */
bool NotifiedWorkerThread::notifyLater(uint32_t delay, uint32_t v, bool overwrite)
{
    bool didIt = notify(v, overwrite);

    if (didIt) {                   // If we didn't already have something queued, override the delay to be larger
        setIntervalFromNow(delay); // a new version of setInterval relative to the current time
        if (debugNotification) {
            LOG_DEBUG("delaying notification %u\n", delay);
        }
    }

    return didIt;
}

void NotifiedWorkerThread::checkNotification()
{
    auto n = notification;
    notification = 0; // clear notification
    if (n) {
        onNotify(n);
    }
}

int32_t NotifiedWorkerThread::runOnce()
{
    enabled = false; // Only run once per notification
    checkNotification();

    return RUN_SAME;
}

} // namespace concurrency