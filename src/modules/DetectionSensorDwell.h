#pragma once

#include <stdint.h>

// Pure dwell/confirm helper for DetectionSensorModule.
// pinActive must already account for trigger polarity (active-high vs active-low).
// When minimum_detect_secs == 0, behaves like legacy immediate detection.
inline bool detectionSensorUpdateDwell(bool pinActive, uint32_t minimum_detect_secs, uint32_t nowMs, bool &dwellArmed,
                                       uint32_t &dwellStartedMs)
{
    if (!pinActive) {
        dwellArmed = false;
        return false;
    }
    if (minimum_detect_secs == 0)
        return true;

    if (!dwellArmed) {
        dwellArmed = true;
        dwellStartedMs = nowMs;
        return false;
    }

    // Unsigned subtraction is rollover-safe (same approach as Throttle).
    return (nowMs - dwellStartedMs) >= (minimum_detect_secs * 1000UL);
}

// Elapsed continuous-active time while dwell is armed; 0 if not armed / no dwell.
inline uint32_t detectionSensorDwellElapsedMs(bool dwellArmed, uint32_t dwellStartedMs, uint32_t nowMs)
{
    if (!dwellArmed)
        return 0;
    return nowMs - dwellStartedMs;
}

// Total presence window for a confirmed episode (first active sample → clear).
inline uint32_t detectionSensorActiveMs(uint32_t episodeStartMs, uint32_t clearMs)
{
    return clearMs - episodeStartMs;
}

// Episode start: dwell arm time when confirming, else first pin-active sample.
inline uint32_t detectionSensorEpisodeStartMs(uint32_t minimum_detect_secs, uint32_t dwellStartedMs, uint32_t pinActiveStartedMs)
{
    return minimum_detect_secs > 0 ? dwellStartedMs : pinActiveStartedMs;
}
