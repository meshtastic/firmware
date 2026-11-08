#include "NmeaTimeEstimator.h"

#include <algorithm>

namespace
{
constexpr int64_t FORWARD_STEP_MIN_MS = 2;              // smaller lags are within the millisecond clock's own floor
constexpr uint64_t RESYNC_INTERVAL_MS = 10 * 60 * 1000; // 20 ppm crystal error is 12 ms over this
constexpr uint32_t RESYNC_MIN_SAMPLES = 5;
} // namespace

NmeaArrivalBounds nmeaSentenceArrival(const NmeaDrain &drain, uint32_t lastIndex, uint32_t length)
{
    NmeaArrivalBounds b;
    const int64_t byteUs = drain.byteTimeUs;
    const int64_t lastToDollarUs = length > 0 ? (int64_t)(length - 1) * byteUs : 0;

    // Every byte read after this one also had to cross the line before endMs; +1 ms because endMs is a floor.
    const int64_t bytesAfter = drain.bytes > lastIndex ? (int64_t)(drain.bytes - 1 - lastIndex) : 0;
    b.hiUs = (int64_t)drain.endMs * 1000 + 1000 - bytesAfter * byteUs - lastToDollarUs;

    // Nothing read in this drain was readable when the previous one saw the buffer empty.
    if (byteUs > 0 && drain.prevEndMs != 0) {
        b.loUs = (int64_t)drain.prevEndMs * 1000 - drain.holdBackUs + (int64_t)lastIndex * byteUs - lastToDollarUs;
        b.hasLo = true;
    }
    return b;
}

void NmeaTimeEstimator::reset()
{
    *this = NmeaTimeEstimator();
}

void NmeaTimeEstimator::addSample(int64_t utcMs, const NmeaArrivalBounds &arrival)
{
    const int64_t utcUs = utcMs * 1000;
    const int64_t hi = arrival.hiUs - utcUs;
    const int64_t lo = arrival.loUs - utcUs;

    if (samples > 0 && arrival.hiUs - windowStartUs <= (int64_t)MAX_WINDOW_MS * 1000) {
        const int64_t newHi = std::min(offsetHiUs, hi);
        const bool newHaveLo = haveLo || arrival.hasLo;
        const int64_t newLo = (haveLo && arrival.hasLo) ? std::max(offsetLoUs, lo) : (haveLo ? offsetLoUs : lo);
        // An empty intersection means the module's time stepped or a bound was wrong: start over from this sample.
        if (!newHaveLo || newLo <= newHi) {
            offsetHiUs = newHi;
            offsetLoUs = newLo;
            haveLo = newHaveLo;
            samples++;
            return;
        }
    }

    offsetHiUs = hi;
    offsetLoUs = lo;
    haveLo = arrival.hasLo;
    samples = 1;
    windowStartUs = arrival.hiUs;
}

uint64_t NmeaTimeEstimator::utcMillisAt(uint64_t monoMs) const
{
    return (uint64_t)(((int64_t)monoMs * 1000 - offsetHiUs) / 1000);
}

uint32_t NmeaTimeEstimator::uncertaintyUs() const
{
    if (!haveLo)
        return UINT32_MAX;
    const int64_t width = offsetHiUs - offsetLoUs;
    return width >= (int64_t)UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)width;
}

bool shouldApplyNmeaTime(bool clockIsGps, int64_t clockAheadMs, uint64_t msSinceApplied, uint32_t samples, bool ppsRecent)
{
    if (!clockIsGps)
        return true;
    // The estimate never runs ahead of the module, so a clock behind it is wrong and stepping forward only helps.
    if (clockAheadMs <= -FORWARD_STEP_MIN_MS)
        return true;
    // A PPS-pinned clock is ahead of the estimate by the module's output latency, and right to be
    if (ppsRecent)
        return false;
    // A clock ahead may be right; step it back only now and then, to undo crystal drift.
    return msSinceApplied >= RESYNC_INTERVAL_MS && samples >= RESYNC_MIN_SAMPLES;
}
