#include "PpsDiscipline.h"

void PpsDiscipline::reset()
{
    havePrev = false;
    run = 0;
}

PpsEdgeResult PpsDiscipline::onEdge(uint64_t edgeMonoUs, uint64_t clockUtcUs, bool fix)
{
    PpsEdgeResult r;
    if (!fix) {
        reset();
        r.reason = "no fix";
        return r;
    }

    const uint64_t secondUs = (clockUtcUs + 500000) / 1000000 * 1000000;
    const int64_t errorUs = (int64_t)(clockUtcUs - secondUs);
    if (errorUs > MAX_CLOCK_ERROR_US || errorUs < -MAX_CLOCK_ERROR_US) {
        reset();
        r.reason = "clock too far from a second boundary";
        return r;
    }

    bool consistent = false;
    if (havePrev && secondUs > prevUtcUs && secondUs - prevUtcUs <= MAX_GAP_US && edgeMonoUs > prevMonoUs) {
        const int64_t utcGap = (int64_t)(secondUs - prevUtcUs);
        const int64_t monoGap = (int64_t)(edgeMonoUs - prevMonoUs);
        const int64_t tolerance = utcGap / 1000000 * INTERVAL_TOLERANCE_PPM;
        consistent = monoGap - utcGap <= tolerance && utcGap - monoGap <= tolerance;
    }
    havePrev = true;
    prevMonoUs = edgeMonoUs;
    prevUtcUs = secondUs;
    if (!consistent) {
        // A first edge, or one that does not follow the last: it may start a run but cannot be trusted alone
        run = 0;
        r.reason = "no consistent previous edge";
        return r;
    }
    run++;

    if (!haveAnchor) {
        haveAnchor = true;
        anchorMonoUs = edgeMonoUs;
        anchorUtcUs = secondUs;
    } else if (secondUs > anchorUtcUs && edgeMonoUs > anchorMonoUs) {
        const uint64_t spanUs = secondUs - anchorUtcUs;
        if (spanUs >= MIN_RATE_SPAN_US) {
            const int64_t gainUs = (int64_t)(edgeMonoUs - anchorMonoUs) - (int64_t)spanUs;
            const int64_t ppb = gainUs * 1000000000 / (int64_t)spanUs;
            if (ppb <= MAX_RATE_PPB && ppb >= -MAX_RATE_PPB) {
                rate = (int32_t)ppb;
                rateValid = true;
            }
            if (spanUs >= MAX_RATE_SPAN_US || ppb > MAX_RATE_PPB || ppb < -MAX_RATE_PPB) {
                anchorMonoUs = edgeMonoUs;
                anchorUtcUs = secondUs;
            }
        }
    }

    r.accepted = true;
    r.utcUs = secondUs;
    r.errorUs = errorUs;
    return r;
}
