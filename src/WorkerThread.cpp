#include "WorkerThread.h"
#include "debug.h"
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

#ifdef DEBUG_STACK
        static uint32_t lastPrint = 0;
        if (millis() - lastPrint > 10 * 1000L) {
            lastPrint = millis();
            meshtastic::printThreadInfo("net");
        }
#endif

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

void NotifiedWorkerThread::block()
{
    xTaskNotifyWait(0,                                          // don't clear notification on entry
                    clearOnRead, &notification, portMAX_DELAY); // Wait forever
}
