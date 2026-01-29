#include "watchdogThread.h"
#include "configuration.h"

#ifdef HAS_HARDWARE_WATCHDOG
WatchdogThread *watchdogThread;

WatchdogThread::WatchdogThread() : OSThread("Watchdog")
{
    setup();
}

void WatchdogThread::feedDog(void)
{
    digitalWrite(HARDWARE_WATCHDOG_DONE, HIGH);
    delay(1);
    digitalWrite(HARDWARE_WATCHDOG_DONE, LOW);
}

int32_t WatchdogThread::runOnce()
{
    LOG_DEBUG("Feeding hardware watchdog");
    feedDog();
    return HARDWARE_WATCHDOG_TIMEOUT_MS;
}

bool WatchdogThread::setup()
{
    LOG_DEBUG("init hardware watchdog");
    pinMode(HARDWARE_WATCHDOG_WAKE, INPUT);
    pinMode(HARDWARE_WATCHDOG_DONE, OUTPUT);
    delay(1);
    digitalWrite(HARDWARE_WATCHDOG_DONE, LOW);
    delay(1);
    feedDog();
    return true;
}
#endif