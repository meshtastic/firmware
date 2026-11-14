#pragma once

#include <stdint.h>

/**
 * Persisted estimator state, written to /prefs/bme680.dat via SafeFile.
 * Fixed 24-byte little-endian layout; xorHash covers the five preceding words
 * as a semantic guard on top of SafeFile's write-path hash.
 */
struct BME680IaqState {
    uint32_t magic;
    uint8_t version;
    uint8_t warmupRemaining;
    uint8_t reserved[2];
    float lnCeiling;
    uint32_t savedAtSecs; // RTC epoch at save; 0 if no valid RTC
    uint32_t sampleCount;
    uint32_t xorHash;
};

static_assert(sizeof(BME680IaqState) == 24, "BME680IaqState layout must stay fixed for on-disk compatibility");

/**
 * Clean-room IAQ estimator for the BME680/BME688 gas sensor (replaces the
 * proprietary Bosch BSEC library).
 *
 * VOC exposure lowers the sensor's gas resistance. We track a rolling ceiling
 * of humidity-compensated log-resistance ("cleanest air seen recently") and
 * score each sample by its log-distance below that ceiling, mapped onto the
 * 0-500 scale the UI already bands (<=25 Excellent ... >300 Hazardous).
 *
 * Warm-up and burn-in progress are part of the persisted state: a deep-sleep
 * SENSOR node that takes one sample per wake (RAM wiped in between) still
 * converges by restoring and re-serializing across reboots.
 *
 * Pure math on purpose: no Arduino, filesystem, or clock dependencies, so the
 * whole thing is unit-testable on the native host (test_bme680_iaq).
 */
class BME680IaqEstimator
{
  public:
    static constexpr uint32_t MAGIC = 0x42494151; // 'BIAQ'
    static constexpr uint8_t VERSION = 1;

    // Tunables, centralized for the hardware-soak stage. Physical rationale:
    // KH: gas resistance falls roughly exp(-0.035 * %RH); compensate to a 40 %RH reference
    // ALPHA_UP/DOWN: ceiling rises fast toward cleaner air, decays with a ~12 h time
    //   constant at one sample per minute so pollution episodes don't become "normal"
    // LN_FLOOR: baseline can't sit below ln(5 kOhm), the heavily-polluted end of the range
    // LN_CEIL_MAX: sanity bound only -- fresh/very clean sensors legitimately read
    //   1-13 MOhm (Bosch specs to 50 MOhm), so this sits far above at ln(~100 MOhm)
    // LN_RANGE: gas at 1/15 of the baseline maps to IAQ 500
    static constexpr float KH = 0.035f;
    static constexpr float ALPHA_UP = 0.25f;
    static constexpr float ALPHA_DOWN = 1.0f / 720.0f;
    static constexpr float LN_FLOOR = 8.517193f;  // ln(5000)
    static constexpr float LN_CEIL_MAX = 18.4f;   // ln(~1e8)
    static constexpr float LN_RANGE = 2.7080502f; // ln(15)
    static constexpr float HUM_WEIGHT = 0.15f;
    // RH_REF: the KH compensation reference, and the fallback for failed humidity reads
    // RH_COMFORT_MIN/MAX: no humidity penalty inside this band
    // RH_DEV_NORM: deviation that earns the full penalty (== 100 - RH_COMFORT_MAX; the dry
    //   side's maximum deviation is only RH_COMFORT_MIN, so it intentionally caps at 75%)
    static constexpr float RH_REF = 40.0f;
    static constexpr float RH_COMFORT_MIN = 30.0f;
    static constexpr float RH_COMFORT_MAX = 60.0f;
    static constexpr float RH_DEV_NORM = 40.0f;
    static constexpr uint32_t WARMUP_DISCARD = 3;                    // first-ever samples, while the heater element settles
    static constexpr uint32_t BURN_IN_SAMPLES = 30;                  // no output until the baseline has this much history
    static constexpr uint32_t STATE_MAX_AGE_SECS = 7 * 24 * 60 * 60; // a week-old baseline says nothing about today's air

    /**
     * Feed one sample. Returns true and writes *iaqOut (0-500) once the
     * estimator has enough history; returns false during warm-up/burn-in or
     * for invalid readings.
     */
    bool update(float gasOhms, float relativeHumidity, uint16_t *iaqOut);

    /// Burn-in complete: output is available
    bool ready() const { return sampleCount >= BURN_IN_SAMPLES; }

    // Progress accessors, used by the sensor to decide when persisting is worthwhile
    uint32_t samplesFed() const { return sampleCount; }
    uint32_t warmupLeft() const { return warmupRemaining; }

    void serialize(BME680IaqState *out, uint32_t nowSecs) const;

    /**
     * Adopt persisted state, including warm-up/burn-in progress (warm-up is
     * NOT re-armed: the persisted counters are the source of truth). Returns
     * false and leaves the estimator untouched on magic, version, hash, or
     * range mismatch, or if the state is older than STATE_MAX_AGE_SECS (only
     * checkable when both timestamps are valid).
     */
    bool restore(const BME680IaqState &in, uint32_t nowSecs);

  private:
    static uint32_t computeHash(const BME680IaqState &s);

    float lnCeiling = 0.0f;
    uint32_t sampleCount = 0; // samples fed to the baseline (excludes warm-up discards)
    uint32_t warmupRemaining = WARMUP_DISCARD;
    bool seeded = false;
};
