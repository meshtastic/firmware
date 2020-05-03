#pragma once

#include <Arduino.h>

typedef void (*PendableFunction)(void *pvParameter1, uint32_t ulParameter2);

/**
 * Schedule a callback to run.  The callback must _not_ block, though it is called from regular thread level (not ISR)
 *
 * NOTE! ESP32 implementation is busted - always waits 0 ticks
 * 
 * @return true if successful, false if the timer fifo is too full.
 */
 bool scheduleOSCallback(PendableFunction callback, void *param1, uint32_t param2, uint32_t delayMsec);


/// Uses a hardware timer, but calls the handler in _interrupt_ context
bool scheduleHWCallback(PendableFunction callback, void *param1, uint32_t param2, uint32_t delayMsec);