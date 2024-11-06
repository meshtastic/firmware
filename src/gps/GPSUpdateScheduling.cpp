#include "GPSUpdateScheduling.h"

#include "Default.h"

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
}

// Clear old lock-time prediction data.
// When re-enabling GPS with user button.
void GPSUpdateScheduling::reset()
{
    searchStartedMs = 0;
    searchEndedMs = 0;
    searchCount = 0;
    predictedMsToGetLock = 0;
}

// How many milliseconds before we should next search for GPS position
// Used by GPS hardware directly, to enter timed hardware sleep
uint32_t GPSUpdateScheduling::msUntilNextSearch()
{
    uint32_t now = millis();

    // Target interval (seconds), between GPS updates
    uint32_t updateInterval = Default::getConfiguredOrDefaultMs(config.position.gps_update_interval, default_gps_update_interval);

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
    uint32_t minimumOrConfiguredSecs =
        Default::getConfiguredOrMinimumValue(config.position.position_broadcast_secs, default_broadcast_interval_secs);
    uint32_t maxSearchMs = Default::getConfiguredOrDefaultMs(minimumOrConfiguredSecs, default_broadcast_interval_secs);
    // If broadcast interval set to max, no such thing as "too long"
    if (maxSearchMs == UINT32_MAX)
        return false;

    // If we've been searching longer than our position broadcast interval: that's too long
    else if (elapsedSearchMs() > maxSearchMs)
        return true;

    // Otherwise, not too long yet!
    else
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
