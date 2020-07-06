#include "WorkerThread.h"
#include "timing.h"

namespace concurrency {

void WorkerThread::doRun()
{
    while (!wantExit) {
        block();

#ifdef DEBUG_STACK
        static uint32_t lastPrint = 0;
        if (timing::millis() - lastPrint > 10 * 1000L) {
            lastPrint = timing::millis();
            uint32_t taskHandle = reinterpret_cast<uint32_t>(xTaskGetCurrentTaskHandle());
            DEBUG_MSG("printThreadInfo(%s) task: %" PRIx32 " core id: %u min free stack: %u\n", "thread", taskHandle, xPortGetCoreID(),
              uxTaskGetStackHighWaterMark(nullptr));
        }
#endif

        loop();
    }
}

} // namespace concurrency
