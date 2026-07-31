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
    /// directly: that inverts while the deadline sits on the far side of the 32-bit wrap, so the
    /// action either fires immediately or blocks for about the interval it should have waited.
    ///
    /// Use this when the site stores a deadline; use hasElapsed() when it stores the time of the
    /// last event, which allows the full ~49.7 day range instead of ~24.8 days ahead.
    ///
    /// Callers that overload the deadline with an "inactive" sentinel (0, or UINT32_MAX) MUST test
    /// for that separately, first: every such value is arithmetically far in the past, so it reads
    /// as passed.
    ///
    /// TODO(deadline-type): mistake-proof that MUST by giving a deadline its own one-field type -
    /// Deadline::in(ms) / .armed() / .passed() / .disarm(). The sentinel then cannot be forged by a
    /// hand-built `now + interval`, and "armed" stays a question separate from "passed", which is
    /// the split that must survive: which way "inactive" falls is the caller's to decide. Same size
    /// and cost; the work is the sites marked TODO(deadline-type), which today spell the sentinel
    /// four ways - 0, UINT32_MAX, 0-means-forever and 0-means-due-now.
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