#pragma once

#include <Arduino.h>

typedef void (*PendableFunction)(void *pvParameter1, uint32_t ulParameter2);

/// Uses a hardware timer, but calls the handler in _interrupt_ context
bool scheduleHWCallback(PendableFunction callback, void *param1, uint32_t param2, uint32_t delayMsec);