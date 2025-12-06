#pragma once

#include "configuration.h"

// Encapsulates code responsible for the timing of GPS updates
class GPSUpdateScheduling
{
  public:
    // Marks the time of these events, for calculation use
    void informSearching();
    void informGotLock(); // Predicted lock-time is recalculated here

    void reset();           // Reset the prediction - after GPS::disable() / GPS::enable()
    bool isUpdateDue();     // Is it time to begin searching for a GPS position?
    bool searchedTooLong(); // Have we been searching for too long?

    uint32_t msUntilNextSearch(); // How long until we need to begin searching for a GPS? Info provided to GPS hardware for sleep
    uint32_t elapsedSearchMs();   // How long have we been searching so far?
    uint32_t predictedSearchDurationMs(); // How long do we expect to spend searching for a lock?

  private:
    void updateLockTimePrediction(); // Called from informGotLock
    uint32_t searchStartedMs = 0;
    uint32_t searchEndedMs = 0;
    uint32_t searchCount = 0;
    uint32_t predictedMsToGetLock = 0;

    const float weighting = 0.2; // Controls exponential smoothing of lock-times prediction. 20% weighting of "latest lock-time".
};