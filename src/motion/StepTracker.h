#pragma once

#include <cmath>
#include <stdint.h>

class StepTracker
{
  public:
    void setTracking(bool enabled, uint64_t now)
    {
        tracking = enabled;
        if (enabled)
            reset(now);
    }

    void reset(uint64_t now)
    {
        startedAt = now;
        total = 0;
        baseline = 0;
        armed = false;
        hasPeak = false;
        walking = false;
    }

    void sample(float x, float y, float z, uint64_t now)
    {
        if (!tracking)
            return;
        const float magnitude = std::sqrt(x * x + y * y + z * z);
        if (!std::isfinite(magnitude) || magnitude <= 0)
            return;

        if (baseline == 0 || now - lastSample > 250) {
            baseline = magnitude;
            armed = false;
            hasPeak = false;
            walking = false;
        }
        lastSample = now;
        baseline += 0.04f * (magnitude - baseline);
        const float deviation = (magnitude - baseline) / baseline;
        if (deviation < 0.035f)
            armed = true;
        if (!armed || deviation < 0.13f)
            return;

        armed = false;
        const uint64_t interval = now - lastPeak;
        if (hasPeak && interval < 250)
            return;
        // Require two plausible peaks before treating movement as walking.
        if (hasPeak && interval <= 1500) {
            total += walking ? 1 : 2;
            walking = true;
        } else {
            walking = false;
        }
        lastPeak = now;
        hasPeak = true;
    }

    void hardwareTotal(uint32_t count)
    {
        if (tracking && hardwareBaselineValid && count >= lastHardwareCount)
            total += count - lastHardwareCount;
        lastHardwareCount = count;
        hardwareBaselineValid = true;
    }

    void invalidateHardwareBaseline() { hardwareBaselineValid = false; }

    bool isTracking() const { return tracking; }
    uint32_t steps() const { return total; }
    uint64_t elapsedMs(uint64_t now) const { return now - startedAt; }

  private:
    uint64_t startedAt = 0, lastPeak = 0, lastSample = 0;
    uint32_t total = 0, lastHardwareCount = 0;
    float baseline = 0;
    bool tracking = false, armed = false, hasPeak = false, walking = false;
    bool hardwareBaselineValid = true;
};
