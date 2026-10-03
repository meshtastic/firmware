#include "concurrency/RadioTask.h"

#ifdef MESHTASTIC_RADIO_TASK

#include "concurrency/OSThread.h"
#include "configuration.h"
#include <assert.h>

#if !defined(HAS_FREE_RTOS) || defined(ARCH_PORTDUINO) || !(defined(ARCH_ESP32) || defined(ARCH_NRF52))
#error "MESHTASTIC_RADIO_TASK is a bench flag for ESP32 and nRF52: it runs the radio thread from a FreeRTOS task"
#endif

// ESP-IDF counts a task's stack in bytes, the other ports in words
#ifdef ARCH_ESP32
#define RADIO_TASK_STACK 6144
#else
#define RADIO_TASK_STACK 1536
#endif

namespace concurrency
{

ThreadController radioController;
InterruptableDelay radioDelay;

namespace
{
SemaphoreHandle_t radioMutex;
// Only the owner writes these while it holds the mutex, so the owner's own check needs no lock
TaskHandle_t volatile radioMutexOwner;
uint32_t radioMutexDepth;
TaskHandle_t radioTask;
// The thread that last took the lock from another task, and what the radio task last waited on it. Bench diagnostics:
// written and read without a lock, and only read after the TX they describe.
const char *volatile radioMutexHolderName = "none";
volatile uint32_t radioLockWaitUs;
const char *volatile radioLockHolder = "none";

void radioTaskMain(void *)
{
    for (;;) {
        radioTaskLock();
        const long delayMsec = radioController.runOrDelay();
        radioTaskUnlock();
        radioDelay.delay(delayMsec);
    }
}
} // namespace

void radioTaskLock()
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (radioMutexOwner == self) {
        radioMutexDepth++;
        return;
    }
    // Created on first use, which is on the loop during setup, before the task exists. A mutex, not a binary semaphore,
    // so a lower task holding it is raised to the radio task's priority while the radio waits.
    if (!radioMutex)
        radioMutex = xSemaphoreCreateMutex();
    if (radioTask && self == radioTask) {
        uint32_t waitedUs = 0;
        const char *holder = "none";
        if (xSemaphoreTake(radioMutex, 0) != pdTRUE) {
            holder = radioMutexHolderName;
            const uint32_t t0 = micros();
            xSemaphoreTake(radioMutex, portMAX_DELAY);
            waitedUs = micros() - t0;
        }
        radioLockWaitUs = waitedUs;
        radioLockHolder = holder;
    } else {
        xSemaphoreTake(radioMutex, portMAX_DELAY);
        const OSThread *thread = OSThread::current();
        radioMutexHolderName = thread ? thread->ThreadName.c_str() : "loop";
    }
    radioMutexOwner = self;
    radioMutexDepth = 1;
}

void radioTaskUnlock()
{
    if (--radioMutexDepth)
        return;
    radioMutexOwner = nullptr;
    xSemaphoreGive(radioMutex);
}

bool inRadioTask()
{
    return radioTask && xTaskGetCurrentTaskHandle() == radioTask;
}

void startRadioTask()
{
    static bool tried = false;
    if (tried)
        return;
    tried = true;
    radioController.ThreadName = "radioController";
    radioTaskLock(); // creates the mutex before a second task can race for it
    radioTaskUnlock();
    // One above the loop, and below the RX readout task (loop + 2), so a received frame is read out first
    UBaseType_t priority = uxTaskPriorityGet(nullptr) + 1;
    if (priority > configMAX_PRIORITIES - 1)
        priority = configMAX_PRIORITIES - 1;
    TaskHandle_t task = nullptr;
    int core = -1; // unpinned
#ifdef ARCH_ESP32
    // The loop's core, as the readout task: the radio preempts the loop as on one core
    core = xPortGetCoreID();
    if (xTaskCreatePinnedToCore(radioTaskMain, "Radio", RADIO_TASK_STACK, nullptr, priority, &task, core) != pdPASS)
        task = nullptr;
#else
    if (xTaskCreate(radioTaskMain, "Radio", RADIO_TASK_STACK, nullptr, priority, &task) != pdPASS)
        task = nullptr;
#endif
    radioTask = task;
    LOG_INFO("Radio task %s, priority %u (loop %u), core %d", task ? "started" : "not started", (unsigned)priority,
             (unsigned)uxTaskPriorityGet(nullptr), core);
    // Nothing else runs the radio thread: without the task the radio is dead, and the bench must not mistake that for loss
    assert(task);
}

uint32_t radioTaskLockWaitUs()
{
    return radioLockWaitUs;
}

const char *radioTaskLockHolder()
{
    return radioLockHolder;
}

uint32_t radioTaskStackFree()
{
#if INCLUDE_uxTaskGetStackHighWaterMark
    if (!radioTask)
        return 0;
#ifdef ARCH_ESP32
    return uxTaskGetStackHighWaterMark(radioTask);
#else
    return uxTaskGetStackHighWaterMark(radioTask) * sizeof(StackType_t);
#endif
#else
    return 0;
#endif
}

} // namespace concurrency

#endif
