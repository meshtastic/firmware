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

    /// True once an absolute deadline has arrived. Use this rather than comparing against millis()
    /// directly, which breaks for ~24 days after the 32-bit wrap - either stalling the action or
    /// firing it immediately, depending on which side wrapped.
    ///
    /// Use this when the site stores a deadline; use hasElapsed() when it stores the time of the
    /// last event, which allows the full ~49.7 day range instead of ~24.8 days ahead.
    ///
    /// Callers that overload the deadline with an "inactive" sentinel (0, or UINT32_MAX) MUST test
    /// for that separately, first: every such value is arithmetically far in the past, so it reads
    /// as passed.
    static bool deadlinePassed(uint32_t deadlineMs);

    /// deadlinePassed() against a caller-supplied "now", for a loop that snapshots the time once and
    /// tests many deadlines against it. Same range limit and sentinel rules as above.
    static bool deadlinePassedAt(uint32_t nowMs, uint32_t deadlineMs)
    {
        // Passed iff now - deadline has not wrapped past 2^31 ms; further-ahead deadlines land in
        // the top half. Not an int32_t cast, which is implementation-defined beyond INT32_MAX.
        return (uint32_t)(nowMs - deadlineMs) < 0x80000000u;
    }
};