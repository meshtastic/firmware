#include "BME680IaqEstimator.h"

// std::clamp rather than meshUtils.h's clamp: that header drags in Arduino.h,
// and this file must stay compilable standalone on a dev host (see the replay
// harness in bin/bme680_iaq_replay.cpp)
#include <algorithm>
#include <math.h>
#include <string.h>

bool BME680IaqEstimator::update(float gasOhms, float relativeHumidity, uint16_t *iaqOut)
{
    if (!(isfinite(gasOhms) && gasOhms > 0.0f))
        return false;

    // A failed humidity read must not poison the baseline: fall back to the
    // reference, which makes both compensation terms no-ops
    float rh = isfinite(relativeHumidity) ? std::clamp(relativeHumidity, 0.0f, 100.0f) : RH_REF;

    if (warmupRemaining > 0) {
        warmupRemaining--;
        return false;
    }

    float x = logf(gasOhms) + KH * (rh - RH_REF);
    x = std::clamp(x, LN_FLOOR - LN_RANGE, LN_CEIL_MAX);

    if (!seeded) {
        lnCeiling = std::clamp(x, LN_FLOOR, LN_CEIL_MAX);
        seeded = true;
    } else {
        float alpha = (x > lnCeiling) ? ALPHA_UP : ALPHA_DOWN;
        lnCeiling = std::clamp(lnCeiling + alpha * (x - lnCeiling), LN_FLOOR, LN_CEIL_MAX);
    }

    if (sampleCount < UINT32_MAX)
        sampleCount++;
    if (sampleCount < BURN_IN_SAMPLES)
        return false;

    float below = lnCeiling - x;
    if (below < 0.0f)
        below = 0.0f;
    float gasScore = std::clamp(below / LN_RANGE, 0.0f, 1.0f) * 500.0f;

    // Comfort-band penalty: only outside the band, so ordinary indoor humidity
    // can't keep IAQ away from the "Excellent" band
    float humDeviation = rh < RH_COMFORT_MIN ? RH_COMFORT_MIN - rh : (rh > RH_COMFORT_MAX ? rh - RH_COMFORT_MAX : 0.0f);
    float humScore = std::clamp(humDeviation / RH_DEV_NORM, 0.0f, 1.0f) * 500.0f;

    *iaqOut = (uint16_t)lroundf(std::clamp(gasScore + HUM_WEIGHT * humScore, 0.0f, 500.0f));
    return true;
}

uint32_t BME680IaqEstimator::computeHash(const BME680IaqState &s)
{
    uint32_t words[5];
    memcpy(words, &s, sizeof(words));
    return words[0] ^ words[1] ^ words[2] ^ words[3] ^ words[4];
}

void BME680IaqEstimator::serialize(BME680IaqState *out, uint32_t nowSecs) const
{
    memset(out, 0, sizeof(*out));
    out->magic = MAGIC;
    out->version = VERSION;
    out->warmupRemaining = (uint8_t)warmupRemaining;
    out->lnCeiling = lnCeiling;
    out->savedAtSecs = nowSecs;
    out->sampleCount = sampleCount;
    out->xorHash = computeHash(*out);
}

bool BME680IaqEstimator::restore(const BME680IaqState &in, uint32_t nowSecs)
{
    if (in.magic != MAGIC || in.version != VERSION)
        return false;
    if (in.xorHash != computeHash(in))
        return false;
    // The ceiling only exists once a sample has been accepted (sampleCount > 0);
    // pure warm-up progress is persisted with lnCeiling still at 0
    bool hasBaseline = in.sampleCount > 0;
    if (hasBaseline && !(isfinite(in.lnCeiling) && in.lnCeiling >= LN_FLOOR && in.lnCeiling <= LN_CEIL_MAX))
        return false;
    // Staleness is only judgeable when the state was stamped with a valid RTC
    // and we have one now; a week-old baseline says nothing about today's air
    if (in.savedAtSecs != 0 && nowSecs != 0 && nowSecs >= in.savedAtSecs && (nowSecs - in.savedAtSecs) > STATE_MAX_AGE_SECS)
        return false;

    lnCeiling = in.lnCeiling;
    sampleCount = in.sampleCount;
    warmupRemaining = in.warmupRemaining <= WARMUP_DISCARD ? in.warmupRemaining : WARMUP_DISCARD;
    seeded = hasBaseline;
    return true;
}
