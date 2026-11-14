#include "LnaAutoSelector.h"

void LnaAutoSelector::reset(bool startLnaOn)
{
    lnaOn = startLnaOn;
    sampleCount = 0;
    deltaSum = 0;
    lastBenefitTenthsDb = 0;
}

bool LnaAutoSelector::addSample(int16_t rssiLnaOn, int16_t rssiLnaOff)
{
    deltaSum += rssiLnaOn - rssiLnaOff;
    if (++sampleCount < WINDOW_SAMPLES)
        return false;

    // Scaled by the sample count so the thresholds compare in integers
    const int32_t n = sampleCount;
    const int32_t benefitScaled = (int32_t)lnaGainDb * n - deltaSum;
    lastBenefitTenthsDb = benefitScaled * 10 / n;
    sampleCount = 0;
    deltaSum = 0;

    if (benefitScaled > MAX_PLAUSIBLE_BENEFIT_DB * n)
        lnaOn = false;
    else if (benefitScaled >= ENABLE_BENEFIT_DB * n)
        lnaOn = true;
    else if (benefitScaled <= DISABLE_BENEFIT_DB * n)
        lnaOn = false;
    return true;
}
