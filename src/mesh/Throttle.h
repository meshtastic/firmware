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

    /// True once an absolute deadline has arrived. Use this instead of comparing against millis()
    /// directly: `millis() > deadlineMs` breaks for ~24 days after the 32-bit millis() wrap, either
    /// stalling the action or firing it immediately, depending on which side wrapped.
    ///
    /// Use this form when the site stores a deadline it cannot recompute; use hasElapsed() when it
    /// stores the time of the last event, which is the cheaper shape.
    ///
    /// The comparison is an unsigned half-range test, matching NextHopRouter::doRetransmissions().
    /// It reads deadlines more than ~24.8 days in the future as already passed - the longest
    /// interval anywhere in this firmware is 24 hours, a ~50x margin.
    ///
    /// Callers that overload the deadline with an "inactive" sentinel (0, or UINT32_MAX) MUST test
    /// for that separately, before calling this. Every such value reads as "long since passed"
    /// here, because that is what it arithmetically is.
    static bool deadlinePassed(uint32_t deadlineMs);
};