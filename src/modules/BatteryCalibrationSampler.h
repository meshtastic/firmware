#pragma once

#include "configuration.h"

#if HAS_SCREEN

#include "concurrency/OSThread.h"

class BatteryCalibrationSampler : private concurrency::OSThread
{
  public:
    struct BatterySample {
        uint16_t voltageMv;
        uint32_t timestampMs;
    };

    static constexpr uint16_t kMaxSamples = 1024;
    static constexpr uint32_t kBaseSampleIntervalMs = 5000;
    BatteryCalibrationSampler();

    void startSampling();
    void stopSampling();
    bool isSampling() const { return active; }
    void resetSamples();
    void getSamples(const BatterySample *&samplesOut, uint16_t &countOut, uint16_t &startOut) const;
    uint32_t getSampleIntervalMs() const { return sampleIntervalMs; }

  protected:
    int32_t runOnce() override;

  private:
    BatterySample samples[kMaxSamples]{};
    uint16_t sampleCount = 0;
    uint16_t sampleStart = 0;
    uint32_t lastSampleMs = 0;
    uint32_t sampleIntervalMs = kBaseSampleIntervalMs;
    bool active = false;

    void appendSample(uint16_t voltageMv, uint32_t nowMs);
    void downsampleSamples();
};

extern BatteryCalibrationSampler *batteryCalibrationSampler;
#endif