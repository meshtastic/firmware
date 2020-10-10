#include "NotifiedWorkerThread.h"
#include <assert.h>

namespace concurrency
{

/**
 * Notify this thread so it can run
 */
IRAM_ATTR void NotifiedWorkerThread::notify(uint32_t v, bool overwrite) {
    
}

/**
 * Notify from an ISR
 *
 * This must be inline or IRAM_ATTR on ESP32
 */
IRAM_ATTR void NotifiedWorkerThread::notifyFromISR(BaseType_t *highPriWoken, uint32_t v, bool overwrite)
{
    notify(v, overwrite);
}

/**
 * Schedule a notification to fire in delay msecs
 */
void NotifiedWorkerThread::notifyLater(uint32_t delay, uint32_t v, bool overwrite) {

}

uint32_t NotifiedWorkerThread::runOnce() {

}

} // namespace concurrency