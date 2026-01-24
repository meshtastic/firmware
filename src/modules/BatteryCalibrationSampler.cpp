#include "BatteryCalibrationSampler.h"
#include "configuration.h"
#include "modules/BatteryCalibrationModule.h"

#if HAS_SCREEN

#include <Arduino.h>
#include "mesh/NodeDB.h"
#include "power.h"

BatteryCalibrationSampler *batteryCalibrationSampler;

BatteryCalibrationSampler::BatteryCalibrationSampler() : concurrency::OSThread("BatteryCalibrationSampler")
{
    batteryCalibrationSampler = this;
    startSampling();
}

void BatteryCalibrationSampler::startSampling()
{
    active = true;
    enabled = true;
    setIntervalFromNow(0);
}

void BatteryCalibrationSampler::stopSampling()
{
    active = false;
    disable();
}

void BatteryCalibrationSampler::resetSamples()
{
    sampleCount = 0;
    sampleStart = 0;
    lastSampleMs = 0;
    sampleIntervalMs = kBaseSampleIntervalMs;
}

void BatteryCalibrationSampler::getSamples(const BatterySample *&samplesOut, uint16_t &countOut, uint16_t &startOut) const
{
    samplesOut = samples;
    countOut = sampleCount;
    startOut = sampleStart;
}

void BatteryCalibrationSampler::appendSample(uint16_t voltageMv, uint32_t nowMs)
{

    lastSampleMs = nowMs;

    if (sampleCount == kMaxSamples) {
        downsampleSamples();
    }

    const uint16_t index = static_cast<uint16_t>((sampleStart + sampleCount) % kMaxSamples);
    sampleCount = static_cast<uint16_t>(sampleCount + 1);

    samples[index].voltageMv = voltageMv;
    samples[index].timestampMs = nowMs;
}

void BatteryCalibrationSampler::downsampleSamples()
{
    if (sampleCount < 2) {
        return;
    }

    const uint16_t newCount = static_cast<uint16_t>(sampleCount / 2);
    for (uint16_t i = 0; i < newCount; ++i) {
        const uint16_t firstIndex = static_cast<uint16_t>((sampleStart + (2 * i)) % kMaxSamples);
        const uint16_t secondIndex = static_cast<uint16_t>((sampleStart + (2 * i + 1)) % kMaxSamples);
        const uint32_t avgVoltage =
            (static_cast<uint32_t>(samples[firstIndex].voltageMv) + static_cast<uint32_t>(samples[secondIndex].voltageMv)) /
            2U;
        const uint32_t avgTimestamp = (samples[firstIndex].timestampMs + samples[secondIndex].timestampMs) / 2U;
        samples[i].voltageMv = static_cast<uint16_t>(avgVoltage);
        samples[i].timestampMs = avgTimestamp;
    }

    sampleCount = newCount;
    sampleStart = 0;
    sampleIntervalMs = static_cast<uint32_t>(sampleIntervalMs * 2U);
}

int32_t BatteryCalibrationSampler::runOnce()
{
    if (!active) {
        return disable();
    }

    const uint32_t nowMs = millis();
    if (!powerStatus || !powerStatus->getHasBattery()) {
        resetSamples();
        return sampleIntervalMs;
    }

    appendSample(static_cast<uint16_t>(powerStatus->getBatteryVoltageMv()), nowMs);
    if (batteryCalibrationModule) {
        batteryCalibrationModule->handleSampleUpdate();
    }
    return sampleIntervalMs;
}

#endif