#include "WorkerThread.h"
#include <assert.h>

void Thread::start(const char *name, size_t stackSize, uint32_t priority)
{
    auto r = xTaskCreate(callRun, name, stackSize, this, priority, &taskHandle);
    assert(r == pdPASS);
}

void Thread::callRun(void *_this)
{
    ((Thread *)_this)->doRun();
}

void WorkerThread::doRun()
{
    while (!wantExit) {
        block();
        loop();
    }
}

/**
 * Notify this thread so it can run
 */
void NotifiedWorkerThread::notify(uint32_t v, eNotifyAction action)
{
    xTaskNotify(taskHandle, v, action);
}

/**
 * Notify from an ISR
 */
void NotifiedWorkerThread::notifyFromISR(BaseType_t *highPriWoken, uint32_t v, eNotifyAction action)
{
    xTaskNotifyFromISR(taskHandle, v, action, highPriWoken);
}

void NotifiedWorkerThread::block()
{
    xTaskNotifyWait(0,                             // don't clear notification on entry
                    0,                             //  do not reset notification value on read
                    &notification, portMAX_DELAY); // Wait forever
}
