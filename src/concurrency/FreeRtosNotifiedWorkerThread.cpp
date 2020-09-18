#include "NotifiedWorkerThread.h"

#ifdef HAS_FREE_RTOS

namespace concurrency {

/**
 * Notify this thread so it can run
 */
void FreeRtosNotifiedWorkerThread::notify(uint32_t v, eNotifyAction action)
{
    xTaskNotify(taskHandle, v, action);
}

void FreeRtosNotifiedWorkerThread::block()
{
    xTaskNotifyWait(0,                                          // don't clear notification on entry
                    clearOnRead, &notification, portMAX_DELAY); // Wait forever
}

} // namespace concurrency

#endif