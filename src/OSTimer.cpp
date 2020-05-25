#include "OSTimer.h"
#include "configuration.h"

/**
 * Schedule a callback to run.  The callback must _not_ block, though it is called from regular thread level (not ISR)
 *
 * NOTE! xTimerPend... seems to ignore the time passed in on ESP32 and on NRF52
 * The reason this didn't work is bcause xTimerPednFunctCall really isn't a timer function at all - it just means run the callback
 * from the timer thread the next time you have spare cycles.
 *
 * @return true if successful, false if the timer fifo is too full.

bool scheduleOSCallback(PendableFunction callback, void *param1, uint32_t param2, uint32_t delayMsec)
{
    return xTimerPendFunctionCall(callback, param1, param2, pdMS_TO_TICKS(delayMsec));
} */

#ifndef NO_ESP32

// Super skanky quick hack to use hardware timers of the ESP32
static hw_timer_t *timer;
static PendableFunction tCallback;
static void *tParam1;
static uint32_t tParam2;

static void IRAM_ATTR onTimer()
{
    (*tCallback)(tParam1, tParam2);
}

bool scheduleHWCallback(PendableFunction callback, void *param1, uint32_t param2, uint32_t delayMsec)
{
    if (!timer) {
        timer = timerBegin(0, 80, true); // one usec per tick (main clock is 80MhZ on ESP32)
        assert(timer);
        timerAttachInterrupt(timer, &onTimer, true);
    }

    tCallback = callback;
    tParam1 = param1;
    tParam2 = param2;

    timerAlarmWrite(timer, delayMsec * 1000L, false); // Do not reload, we want it to be a single shot timer
    timerRestart(timer);
    timerAlarmEnable(timer);
    return true;
}

#endif