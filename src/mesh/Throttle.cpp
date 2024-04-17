#include "Throttle.h"
#include <Arduino.h>

/// @brief Execute a function throttled to a minimum interval
/// @param lastExecutionMs Pointer to the last execution time in milliseconds
/// @param minumumIntervalMs Minimum execution interval in milliseconds
/// @param throttleFunc Function to execute if the execution is not deferred
/// @param onDefer Default to NULL, execute the function if the execution is deferred
/// @return true if the function was executed, false if it was deferred
bool Throttle::execute(uint32_t *lastExecutionMs, uint32_t minumumIntervalMs, void (*throttleFunc)(void), void (*onDefer)(void))
{
    if (*lastExecutionMs == 0) {
        *lastExecutionMs = millis();
        throttleFunc();
        return true;
    }
    uint32_t now = millis();

    if ((now - *lastExecutionMs) >= minumumIntervalMs) {
        throttleFunc();
        *lastExecutionMs = now;
        return true;
    } else if (onDefer != NULL) {
        onDefer();
    }
    return false;
}