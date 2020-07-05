#include "NotifiedWorkerThread.h"

namespace concurrency {

/**
 * Notify this thread so it can run
 */
void NotifiedWorkerThread::notify(uint32_t v, eNotifyAction action)
{
    xTaskNotify(taskHandle, v, action);
}

void NotifiedWorkerThread::block()
{
    xTaskNotifyWait(0,                                          // don't clear notification on entry
                    clearOnRead, &notification, portMAX_DELAY); // Wait forever
}

} // namespace concurrency
