#pragma once

#include <stdint.h>

/// What PpsDiscipline made of one PPS edge.
struct PpsEdgeResult {
    bool accepted = false;   ///< the edge marks utcUs; the clock should be pinned to it
    uint64_t utcUs = 0;      ///< the UTC second boundary the edge marks
    int64_t errorUs = 0;     ///< what the clock read at the edge minus utcUs
    const char *reason = ""; ///< why an edge was not accepted
};

/// Turns PPS edges into clock corrections.
///
/// A PPS edge marks the top of a UTC second but not which one. The clock, already set from NMEA, names it: the
/// nearest second, which is unambiguous while the clock is within MAX_CLOCK_ERROR_US. An edge is used only while the
/// receiver has a fix (before one, receivers pulse on their own unaligned clock) and when it follows the previous
/// edge by a whole number of seconds, so a glitch on the line cannot move the clock.
///
/// Edges come in short bursts, so the IRQ is not held between them. The monotonic clock's frequency error is
/// measured from an anchor edge that survives between bursts: the longer the baseline, the finer the estimate.
class PpsDiscipline
{
  public:
    static constexpr int64_t MAX_CLOCK_ERROR_US = 400000;           ///< NMEA lag stays well inside this
    static constexpr int64_t INTERVAL_TOLERANCE_PPM = 1000;         ///< crystals are good to ~50 ppm
    static constexpr uint64_t MAX_GAP_US = 10 * 1000000ULL;         ///< consecutive edges further apart start over
    static constexpr uint64_t MIN_RATE_SPAN_US = 16 * 1000000ULL;   ///< shortest baseline for a rate
    static constexpr uint64_t MAX_RATE_SPAN_US = 3600 * 1000000ULL; ///< the anchor moves on after this; rates wander
    static constexpr int64_t MAX_RATE_PPB = 500000;                 ///< more than this is a bad measurement

    /// The receiver slept, lost its fix, or the burst ended: the next edge needs a consistent successor again.
    /// The rate anchor is kept.
    void reset();

    /// edgeMonoUs: monotonic µs at the edge. clockUtcUs: what the clock read then. fix: the receiver reports a fix.
    PpsEdgeResult onEdge(uint64_t edgeMonoUs, uint64_t clockUtcUs, bool fix);

    /// The monotonic clock gains this many ns per second against UTC.
    bool hasRate() const { return rateValid; }
    int32_t ratePpb() const { return rate; }

    /// Edges accepted since the last reset
    uint32_t runLength() const { return run; }

  private:
    bool havePrev = false;
    uint64_t prevMonoUs = 0;
    uint64_t prevUtcUs = 0;
    uint32_t run = 0;

    bool haveAnchor = false;
    uint64_t anchorMonoUs = 0;
    uint64_t anchorUtcUs = 0;
    bool rateValid = false;
    int32_t rate = 0;
};
