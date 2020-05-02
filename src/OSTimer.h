#pragma once

#include <Arduino.h>

typedef void (*PendableFunction)(void *pvParameter1, uint32_t ulParameter2);

/**
 * Schedule a callback to run.  The callback must _not_ block, though it is called from regular thread level (not ISR)
 *
 * @return true if successful, false if the timer fifo is too full.
 */
inline bool scheduleCallback(PendableFunction callback, void *param1, uint32_t param2, uint32_t delayMsec)
{
    return xTimerPendFunctionCall(callback, param1, param2, pdMS_TO_TICKS(delayMsec));
}