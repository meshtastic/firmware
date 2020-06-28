#include "WorkerThread.h"
#include "debug.h"
#include <assert.h>

#ifdef configUSE_PREEMPTION

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
    startWatchdog();

    while (!wantExit) {
        stopWatchdog();
        block();
        startWatchdog();

        // no need - startWatchdog is guaranteed to give us one full watchdog interval
        // serviceWatchdog(); // Let our loop worker have one full watchdog interval (at least) to run

#ifdef DEBUG_STACK
        static uint32_t lastPrint = 0;
        if (millis() - lastPrint > 10 * 1000L) {
            lastPrint = millis();
            meshtastic::printThreadInfo("net");
        }
#endif

        loop();
    }

    stopWatchdog();
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

#endif