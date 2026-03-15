#pragma once

#include <cstdint>

namespace vigil {

/*
 * Heading — Owns the single source of truth for node orientation.
 *
 * State machine:
 *
 *   ┌──────────────┐
 *   │ UNCALIBRATED │  (power-on, no data yet)
 *   └──────┬───────┘
 *          │ magnetometer read succeeds
 *          ▼
 *   ┌──────────────┐
 *   │  MAG_ONLY    │  (±15° accuracy, immediate)
 *   │  conf: LOW   │
 *   └──────┬───────┘
 *          │ acoustic calibration completes
 *          ▼
 *   ┌──────────────┐
 *   │  ACOUSTIC    │  (±2° accuracy, after calm wind)
 *   │  conf: HIGH  │
 *   └──────┬───────┘
 *          │ periodic re-cal or node disturbed (>15° shift)
 *          ▼
 *   ┌──────────────┐
 *   │ RECALIBRATING│  (reverts to MAG_ONLY during recal)
 *   └──────────────┘
 *
 * Both magnetometer.cpp and chirp_receiver.cpp feed INTO this module.
 * This module OWNS the heading_offset output. No DRY violation.
 */

enum class HeadingState : uint8_t {
    UNCALIBRATED = 0,
    MAG_ONLY,
    ACOUSTIC,
    RECALIBRATING,
};

enum class HeadingSource : uint8_t {
    NONE = 0,
    MAGNETOMETER,
    ACOUSTIC,
};

class Heading {
  public:
    // Apply heading offset to a local DoA angle → global bearing
    float toGlobalBearing(float local_azimuth_deg) const;

    // Feed magnetometer heading (called by magnetometer driver)
    void setMagHeading(float mag_north_deg, float declination_deg);

    // Feed acoustic calibration result (called by chirp receiver)
    void setAcousticOffset(float offset_deg);

    // Notify of physical disturbance (>15° sudden shift)
    void notifyDisturbance();

    // Getters for heartbeat payload
    HeadingState getState() const { return state; }
    HeadingSource getSource() const { return source; }
    float getConfidence() const;
    uint32_t getCalAgeMins() const;

  private:
    HeadingState state = HeadingState::UNCALIBRATED;
    HeadingSource source = HeadingSource::NONE;

    float heading_offset_deg = 0.0f;    // Current best offset
    float mag_offset_deg = 0.0f;        // Magnetometer-derived
    float acoustic_offset_deg = 0.0f;   // Acoustic-derived (higher priority)
    uint32_t last_cal_time_ms = 0;
};

} // namespace vigil
