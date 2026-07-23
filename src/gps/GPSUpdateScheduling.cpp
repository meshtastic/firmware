#include "GPSUpdateScheduling.h"

#include "Default.h"

// Sampled from the original `2750 * seconds^1.22` curve (see GPS::down()'s history for context:
// a heuristic manually fitted to power observations from U-blox NEO-6M and M10050,
// https://www.desmos.com/calculator/6gvjghoumr). Piecewise-linear interpolation between these
// points tracks the original formula within ~0.5% for inputs >=10s, within ~1.6% for 5-10s, and
// diverges further (in relative terms only - a couple of seconds absolute) below 5s - all
// negligible next to update intervals measured in tens of seconds to hours.
static constexpr uint32_t kThresholdCurveSecs[] = {0, 5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 300, 450, 600, 900};
static constexpr uint32_t kThresholdCurveMs[] = {0,      19592,  45639,   74845,   106314,  174350,  285925,  406141,
                                                 666053, 946093, 1551548, 2203893, 2893481, 4745172, 6740269, 11053722};
static constexpr size_t kThresholdCurvePoints = sizeof(kThresholdCurveSecs) / sizeof(kThresholdCurveSecs[0]);

// How long does gps_update_interval need to be, for GPS_HARDSLEEP to become more efficient than
// GPS_SOFTSLEEP? Avoids pow() so this heuristic doesn't pull double-precision libm into the image.
uint32_t gpsHardsleepThresholdMs(uint32_t predictedSearchSecs)
{
    if (predictedSearchSecs >= kThresholdCurveSecs[kThresholdCurvePoints - 1])
        return kThresholdCurveMs[kThresholdCurvePoints - 1];

    size_t i = 1;
    while (kThresholdCurveSecs[i] < predictedSearchSecs)
        i++;

    uint32_t x0 = kThresholdCurveSecs[i - 1], x1 = kThresholdCurveSecs[i];
    uint32_t y0 = kThresholdCurveMs[i - 1], y1 = kThresholdCurveMs[i];
    return y0 + (uint32_t)((uint64_t)(y1 - y0) * (predictedSearchSecs - x0) / (x1 - x0));
}

// Mark the time when searching for GPS position begins
void GPSUpdateScheduling::informSearching()
{
    searchStartedMs = millis();
}

// Mark the time when searching for GPS is complete,
// then update the predicted lock-time
void GPSUpdateScheduling::informGotLock()
{
    searchEndedMs = millis();
    LOG_DEBUG("Took %us to get lock", (searchEndedMs - searchStartedMs) / 1000);
    updateLockTimePrediction();
    consecutiveFailures = 0; // Drop back to fast cadence as soon as we acquire any fix
}

// Search finished without obtaining a fix. We still need to mark the end time so
// the next sleep is timed correctly, but we must not feed the timeout duration
// into predictedMsToGetLock - doing so poisons msUntilNextSearch() and causes
// down() to fall into GPS_IDLE, leaving the chip awake on subsequent indoor cycles.
void GPSUpdateScheduling::informSearchFailed()
{
    searchEndedMs = millis();
    consecutiveFailures++;
    LOG_DEBUG("GPS search ended without fix after %us (consecutive failures: %u)", (searchEndedMs - searchStartedMs) / 1000,
              consecutiveFailures);
}

// Clear old lock-time prediction data.
// When re-enabling GPS with user button.
void GPSUpdateScheduling::reset()
{
    searchStartedMs = 0;
    searchEndedMs = 0;
    searchCount = 0;
    predictedMsToGetLock = 0;
    consecutiveFailures = 0;
}

// How many milliseconds before we should next search for GPS position
// Used by GPS hardware directly, to enter timed hardware sleep
uint32_t GPSUpdateScheduling::msUntilNextSearch()
{
    uint32_t now = millis();

    // Target interval (seconds), between GPS updates
    uint32_t updateInterval = Default::getConfiguredOrDefaultMs(config.position.gps_update_interval, default_gps_update_interval);

    // After a failed search, back off: indoors / no-sky environments will keep failing,
    // so wake at most once per broadcast interval rather than once per gps_update_interval.
    // Capped at 1 hour so a user-configured very-long broadcast interval still retries
    // periodically (in case conditions change). Reset on any successful lock.
    if (consecutiveFailures > 0) {
        constexpr uint32_t failureRetryCapMs = 60UL * 60UL * 1000UL; // 1 hour cap
        uint32_t failureSleepMs =
            Default::getConfiguredOrDefaultMs(config.position.position_broadcast_secs, default_broadcast_interval_secs);
        if (failureSleepMs > failureRetryCapMs)
            failureSleepMs = failureRetryCapMs;
        if (updateInterval < failureSleepMs)
            updateInterval = failureSleepMs;
    }

    // Check how long until we should start searching, to hopefully hit our target interval
    uint32_t dueAtMs = searchEndedMs + updateInterval;
    uint32_t compensatedStart = dueAtMs - predictedMsToGetLock;
    int32_t remainingMs = compensatedStart - now;

    // If we should have already started (negative value), start ASAP
    if (remainingMs < 0)
        remainingMs = 0;

    return (uint32_t)remainingMs;
}

// How long have we already been searching?
// Used to abort a search in progress, if it runs unacceptably long
uint32_t GPSUpdateScheduling::elapsedSearchMs()
{
    // If searching
    if (searchStartedMs > searchEndedMs)
        return millis() - searchStartedMs;

    // If not searching - 0ms. We shouldn't really consume this value
    else
        return 0;
}

// Is it now time to begin searching for a GPS position?
bool GPSUpdateScheduling::isUpdateDue()
{
    return (msUntilNextSearch() == 0);
}

// Have we been searching for a GPS position for too long?
bool GPSUpdateScheduling::searchedTooLong()
{
    constexpr uint32_t oneMinuteMs = 60UL * 1000UL;
    constexpr uint32_t maxSearchClampMs = 15UL * oneMinuteMs;   // Hard cap: 15 minutes is always too long
    constexpr uint32_t postFailureSearchMs = 5UL * oneMinuteMs; // Tighter dwell once we know the environment is hostile
    uint32_t elapsed = elapsedSearchMs();

    // Anything over 15 minutes is too long, regardless of the broadcast interval.
    if (elapsed > maxSearchClampMs)
        return true;

    // After a prior failed search, shorten the dwell
    if (consecutiveFailures > 0 && elapsed > postFailureSearchMs)
        return true;

    uint32_t minimumOrConfiguredSecs =
        Default::getConfiguredOrMinimumValue(config.position.position_broadcast_secs, default_broadcast_interval_secs);
    uint32_t maxSearchMs = Default::getConfiguredOrDefaultMs(minimumOrConfiguredSecs, default_broadcast_interval_secs);

    // If we've been searching longer than our position broadcast interval: that's too long
    if (elapsed > maxSearchMs)
        return true;

    // Otherwise, not too long yet!
    return false;
}

// Updates the predicted time-to-get-lock, by exponentially smoothing the latest observation
void GPSUpdateScheduling::updateLockTimePrediction()
{

    // How long did it take to get GPS lock this time?
    // Duration between down() calls
    int32_t lockTime = searchEndedMs - searchStartedMs;
    if (lockTime < 0)
        lockTime = 0;

    // Ignore the first lock-time: likely to be long, will skew data

    // Second locktime: likely stable. Use to initialize the smoothing filter
    if (searchCount == 1)
        predictedMsToGetLock = lockTime;

    // Third locktime and after: predict using exponential smoothing. Respond slowly to changes
    else if (searchCount > 1)
        predictedMsToGetLock = (lockTime * weighting) + (predictedMsToGetLock * (1 - weighting));

    searchCount++; // Only tracked so we can disregard initial lock-times

    LOG_DEBUG("Predict %us to get next lock", predictedMsToGetLock / 1000);
}

// How long do we expect to spend searching for a lock?
uint32_t GPSUpdateScheduling::predictedSearchDurationMs()
{
    return GPSUpdateScheduling::predictedMsToGetLock;
}
