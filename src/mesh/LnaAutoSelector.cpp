#include "LnaAutoSelector.h"
#include <algorithm>

static_assert(LnaAutoSelector::WINDOW_SAMPLES % 2 == 0, "median below assumes an even window");

void LnaAutoSelector::reset(bool startLnaOn)
{
    lnaOn = startLnaOn;
    sampleCount = 0;
    lastBenefitTenthsDb = 0;
}

bool LnaAutoSelector::addSample(int16_t rssiLnaOn, int16_t rssiLnaOff)
{
    rises[sampleCount] = rssiLnaOn - rssiLnaOff;
    if (++sampleCount < WINDOW_SAMPLES)
        return false;
    sampleCount = 0;

    // Median, not mean: a packet arriving during one reading of a pair throws that pair far off
    std::sort(rises, rises + WINDOW_SAMPLES);
    const int32_t riseTenthsDb = (rises[WINDOW_SAMPLES / 2 - 1] + rises[WINDOW_SAMPLES / 2]) * 5;
    lastBenefitTenthsDb = lnaGainDb * 10 - riseTenthsDb;

    if (lastBenefitTenthsDb > maxBenefitDb * 10)
        lnaOn = false;
    else if (lastBenefitTenthsDb >= ENABLE_BENEFIT_DB * 10)
        lnaOn = true;
    else if (lastBenefitTenthsDb <= DISABLE_BENEFIT_DB * 10)
        lnaOn = false;
    return true;
}
