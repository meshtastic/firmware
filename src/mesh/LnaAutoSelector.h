#pragma once
#include <stdint.h>

/**
 * Decides whether an external RX LNA is worth switching in, from paired noise-floor readings taken with it in and
 * bypassed. RSSI is not gain-compensated, so when outside noise dominates the floor rises by the full LNA gain and the
 * LNA buys no sensitivity; when the receiver's own noise dominates it rises by less, and the shortfall is the gain in
 * sensitivity.
 */
class LnaAutoSelector
{
  public:
    static constexpr uint8_t WINDOW_SAMPLES = 10;
    // Hysteresis band on the sensitivity the LNA buys, in dB
    static constexpr int32_t ENABLE_BENEFIT_DB = 2;
    static constexpr int32_t DISABLE_BENEFIT_DB = 1;
    // More than the LNA's noise-figure improvement can explain: its gain is compressed by a strong signal
    static constexpr int32_t MAX_PLAUSIBLE_BENEFIT_DB = 8;

    explicit LnaAutoSelector(int16_t lnaGainDb) : lnaGainDb(lnaGainDb) {}

    void reset(bool startLnaOn);
    /** Add one paired reading; returns true when it completes a window, which may have changed isLnaOn() */
    bool addSample(int16_t rssiLnaOn, int16_t rssiLnaOff);
    bool isLnaOn() const { return lnaOn; }
    /** Sensitivity the LNA bought over the last completed window, in tenths of a dB */
    int32_t getLastBenefitTenthsDb() const { return lastBenefitTenthsDb; }

  private:
    int16_t lnaGainDb;
    bool lnaOn = true;
    uint8_t sampleCount = 0;
    int32_t deltaSum = 0;
    int32_t lastBenefitTenthsDb = 0;
};
