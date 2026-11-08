#pragma once

#include <stdint.h>

/// Bounds, in monotonic microseconds, on when the '$' of one NMEA sentence finished arriving over the UART.
struct NmeaArrivalBounds {
    int64_t loUs = 0;
    int64_t hiUs = 0;
    bool hasLo = false; ///< false where nothing bounds it from below (first drain, or a source with no line rate)
};

/// One UART drain, as whileActive() saw it. A drain reads until available() is 0, then stamps endMs.
struct NmeaDrain {
    uint64_t prevEndMs;  ///< endMs of the previous drain, 0 if unknown
    uint64_t endMs;      ///< Time::getMillisMonotonic() read after the buffer was last seen empty
    uint32_t bytes;      ///< bytes read in this drain
    uint32_t byteTimeUs; ///< line time of one byte; 0 for sources with no line rate (gpsd, bridged UARTs)
    uint32_t holdBackUs; ///< longest a received byte can wait in the UART before available() reports it
};

/// Bound the arrival of a sentence whose terminating byte is byte `lastIndex` of `drain` and which is `length` bytes long.
NmeaArrivalBounds nmeaSentenceArrival(const NmeaDrain &drain, uint32_t lastIndex, uint32_t length);

/// Tracks the offset between the monotonic clock and the UTC that NMEA sentences report, from many sentences.
///
/// Each sentence bounds the offset; the window keeps the intersection. The estimate is the upper bound, so UTC derived
/// from it never runs ahead of the module's own report: it lags by the module's output latency plus what the bounds
/// could not exclude.
class NmeaTimeEstimator
{
  public:
    static constexpr uint64_t MAX_WINDOW_MS = 60 * 1000; ///< drift between the clocks stays well under 2 ms in this time

    void reset();

    /// utcMs: the time the sentence names, centiseconds included. Restarts the window when it is too old or disagrees.
    void addSample(int64_t utcMs, const NmeaArrivalBounds &arrival);

    bool hasEstimate() const { return samples > 0; }
    uint32_t sampleCount() const { return samples; }

    /// UTC ms at monotonic ms `monoMs`. Only meaningful when hasEstimate().
    uint64_t utcMillisAt(uint64_t monoMs) const;

    /// Width of the window in µs: how far the estimate may lag what the bounds allow. UINT32_MAX until bounded below.
    uint32_t uncertaintyUs() const;

  private:
    int64_t offsetLoUs = 0; ///< bounds on (monotonic µs - UTC µs) at a sentence's '$'
    int64_t offsetHiUs = 0;
    bool haveLo = false;
    uint32_t samples = 0;
    int64_t windowStartUs = 0;
};

/// Whether to step the clock to the estimator's time.
/// clockIsGps: the RTC holds GPS-quality time. clockAheadMs: current clock minus estimate.
/// msSinceApplied: since the last step from this source (UINT64_MAX if never).
/// ppsRecent: the clock was pinned to a PPS edge recently, so its lead over the estimate is real.
bool shouldApplyNmeaTime(bool clockIsGps, int64_t clockAheadMs, uint64_t msSinceApplied, uint32_t samples, bool ppsRecent);
