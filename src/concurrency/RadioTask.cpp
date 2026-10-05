#include "concurrency/RadioTask.h"

#ifdef MESHTASTIC_RADIO_TASK

#include "concurrency/OSThread.h"
#include "configuration.h"
#include <assert.h>

#if !defined(HAS_FREE_RTOS) || defined(ARCH_PORTDUINO) || !(defined(ARCH_ESP32) || defined(ARCH_NRF52))
#error "MESHTASTIC_RADIO_TASK runs the radio thread from a FreeRTOS task: ESP32 and nRF52 only"
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
// The line the loop is printing for the radio task (see RedirectablePrint::drainRadioTaskLogs)
const OSThread *loggingForThread;
TaskHandle_t volatile loggingForTask;

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
    xSemaphoreTake(radioMutex, portMAX_DELAY);
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
    // Nothing else runs the radio thread: without the task the radio is dead
    assert(task);
}

void setLoggingFor(const OSThread *thread)
{
    loggingForThread = thread;
    loggingForTask = xTaskGetCurrentTaskHandle();
}

void clearLoggingFor()
{
    loggingForTask = nullptr;
}

bool loggingFor(const OSThread **thread)
{
    if (!loggingForTask || loggingForTask != xTaskGetCurrentTaskHandle())
        return false;
    if (thread)
        *thread = loggingForThread;
    return true;
}

} // namespace concurrency

#endif
