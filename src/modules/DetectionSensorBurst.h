#pragma once

#include <stdint.h>

// Burst coalescing + persistence-gated alerts for DetectionSensorModule.
// RAK12013-style sensors emit fixed OUT pulses with gaps; this merges them into
// one burst while the quiet gap stays under burst_gap_secs.

enum DetectionSensorBurstEvent : uint8_t {
    DetectionSensorBurstEventNone = 0,
    DetectionSensorBurstEventAlert = 1,
    DetectionSensorBurstEventCleared = 2,
};

struct DetectionSensorBurstState {
    bool inBurst = false;
    uint32_t burstStartMs = 0;
    uint32_t accumulatedActiveMs = 0;
    bool segmentOpen = false;
    uint32_t segmentStartMs = 0;
    bool quietArmed = false;
    uint32_t quietStartedMs = 0;
    bool alertSent = false;
};

struct DetectionSensorBurstResult {
    DetectionSensorBurstEvent event = DetectionSensorBurstEventNone;
    uint32_t activeMs = 0;
    uint32_t burstMs = 0;
};

inline uint32_t detectionSensorBurstWallMs(uint32_t burstStartMs, uint32_t nowMs)
{
    return nowMs - burstStartMs;
}

inline uint32_t detectionSensorBurstActiveTotalMs(uint32_t accumulatedActiveMs, bool segmentOpen, uint32_t segmentStartMs,
                                                  uint32_t nowMs)
{
    uint32_t total = accumulatedActiveMs;
    if (segmentOpen)
        total += nowMs - segmentStartMs;
    return total;
}

// dwellConfirmed: first glitch-filtered accept that may open a burst.
// pinActive: raw polarity-adjusted pin (cancels quiet / accumulates presence).
// burst_gap_secs: 0 = end burst on first inactive sample (legacy).
// minimum_alert_secs: 0 = alert when the burst first opens; >0 = alert once
// wall-clock persistence reaches the threshold.
inline DetectionSensorBurstResult detectionSensorUpdateBurst(bool pinActive, bool dwellConfirmed, uint32_t nowMs,
                                                             uint32_t burst_gap_secs, uint32_t minimum_alert_secs,
                                                             uint32_t burstStartCandidateMs, DetectionSensorBurstState &st)
{
    DetectionSensorBurstResult out;

    if (!st.inBurst) {
        if (!dwellConfirmed)
            return out;

        st.inBurst = true;
        st.burstStartMs = burstStartCandidateMs;
        st.accumulatedActiveMs = 0;
        st.segmentOpen = pinActive;
        st.segmentStartMs = burstStartCandidateMs;
        st.quietArmed = false;
        st.alertSent = false;

        if (minimum_alert_secs == 0) {
            st.alertSent = true;
            out.event = DetectionSensorBurstEventAlert;
            out.burstMs = detectionSensorBurstWallMs(st.burstStartMs, nowMs);
            out.activeMs = detectionSensorBurstActiveTotalMs(st.accumulatedActiveMs, st.segmentOpen, st.segmentStartMs, nowMs);
            return out;
        }
    }

    if (pinActive) {
        st.quietArmed = false;
        if (!st.segmentOpen) {
            st.segmentOpen = true;
            st.segmentStartMs = nowMs;
        }
    } else if (st.segmentOpen) {
        st.accumulatedActiveMs += nowMs - st.segmentStartMs;
        st.segmentOpen = false;
    }

    if (!pinActive) {
        if (burst_gap_secs == 0) {
            // legacy: end immediately
        } else if (!st.quietArmed) {
            st.quietArmed = true;
            st.quietStartedMs = nowMs;
        }
    }

    const uint32_t burstMs = detectionSensorBurstWallMs(st.burstStartMs, nowMs);
    const uint32_t activeMs = detectionSensorBurstActiveTotalMs(st.accumulatedActiveMs, st.segmentOpen, st.segmentStartMs, nowMs);

    const bool ending =
        !pinActive && (burst_gap_secs == 0 || (st.quietArmed && (nowMs - st.quietStartedMs) >= (burst_gap_secs * 1000UL)));

    // Prefer alert over clear on the same poll; clear will emit next poll.
    if (!st.alertSent && minimum_alert_secs > 0 && burstMs >= (minimum_alert_secs * 1000UL)) {
        st.alertSent = true;
        out.event = DetectionSensorBurstEventAlert;
        out.burstMs = burstMs;
        out.activeMs = activeMs;
        return out;
    }

    if (ending) {
        out.event = DetectionSensorBurstEventCleared;
        out.activeMs = activeMs;
        out.burstMs = burstMs;
        st = DetectionSensorBurstState{};
    }

    return out;
}
