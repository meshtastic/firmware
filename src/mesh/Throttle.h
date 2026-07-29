#pragma once
#include <cstddef>
#include <cstdint>

class Throttle
{
  public:
    static bool execute(uint32_t *lastExecutionMs, uint32_t minumumIntervalMs, void (*func)(void), void (*onDefer)(void) = NULL);
    static bool isWithinTimespanMs(uint32_t lastExecutionMs, uint32_t intervalMs);

    /// Complement of isWithinTimespanMs(): true once intervalMs has passed since lastExecutionMs.
    /// Boundary is inclusive (>=), mirroring isWithinTimespanMs()'s exclusive <.
    /// Deliberately does not treat lastExecutionMs == 0 as "never run" - callers that use 0 as a
    /// sentinel must test for it separately, so the sentinel never reaches the arithmetic.
    static bool hasElapsed(uint32_t lastExecutionMs, uint32_t intervalMs)
    {
        return !isWithinTimespanMs(lastExecutionMs, intervalMs);
    }
};