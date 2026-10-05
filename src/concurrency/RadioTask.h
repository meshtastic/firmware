#pragma once

// -DMESHTASTIC_RADIO_TASK (ESP32 and nRF52) runs the RadioLib radio thread from its own FreeRTOS task, one priority above
// the main loop, so a main-loop hold can no longer delay a scan, a launch or the TX_DONE handling. The task holds the radio
// lock while the radio thread runs, and the loop takes it with RADIO_TASK_LOCK() wherever it reaches into the radio.
#include "BenchInstrumentation.h"

#ifdef MESHTASTIC_RADIO_TASK

#include "ThreadController.h"
#include "concurrency/InterruptableDelay.h"

namespace concurrency
{

/// Holds only the radio thread; run by the radio task as mainController is by loop()
extern ThreadController radioController;
/// What the radio thread's notifications interrupt, in place of mainDelay
extern InterruptableDelay radioDelay;

/// Recursive: the radio thread can call back into an entry point the loop locks
void radioTaskLock();
void radioTaskUnlock();

/// Once, from the loop: the task's priority is set from the caller's
void startRadioTask();
bool inRadioTask();

class OSThread;
/// While the loop prints a line the radio task logged, the thread that logged it. Set and read on the printing task only;
/// false on any other.
void setLoggingFor(const OSThread *thread);
void clearLoggingFor();
bool loggingFor(const OSThread **thread);
#ifdef MESHTASTIC_LOG_RECORD_MILLIS
/// Bench: with setLoggingFor(), the millis() the line was logged at, for its log record's stamp; false off the printing task
void setLoggedAtMs(uint32_t ms);
bool loggedAtMs(uint32_t *ms);
#endif
#if MESHTASTIC_BENCH_INSTRUMENTATION
/// Bench: the task's stack high-water mark in bytes, 0 before it starts
uint32_t radioTaskStackFree();
#endif

class RadioTaskLockGuard
{
  public:
    RadioTaskLockGuard() { radioTaskLock(); }
    ~RadioTaskLockGuard() { radioTaskUnlock(); }
    RadioTaskLockGuard(const RadioTaskLockGuard &) = delete;
    RadioTaskLockGuard &operator=(const RadioTaskLockGuard &) = delete;
};

} // namespace concurrency

#define RADIO_TASK_LOCK_NAME2(line) radioTaskLockGuard##line
#define RADIO_TASK_LOCK_NAME(line) RADIO_TASK_LOCK_NAME2(line)
#define RADIO_TASK_LOCK() concurrency::RadioTaskLockGuard RADIO_TASK_LOCK_NAME(__LINE__)

#else

#define RADIO_TASK_LOCK()                                                                                                        \
    do {                                                                                                                         \
    } while (0)

#endif
