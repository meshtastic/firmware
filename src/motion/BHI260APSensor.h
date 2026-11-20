#pragma once
#ifndef _BHI260AP_SENSOR_H_
#define _BHI260AP_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && __has_include(<SensorBHI260AP.hpp>)

// Sensor lib
#include <SensorBHI260AP.hpp>
#include <Wire.h>
#include <bosch/BoschSensorDataHelper.hpp>
#include <memory>

class BHI260APSensor : public MotionSensor
{
  private:
    SensorBHI260AP sensor;
    std::unique_ptr<SensorStepCounter> stepCounter;
    bool stepCounterReady = false;
    bool wakeRequested = false;
    bool pollSensor();
#ifdef BHI260AP_INT
    uint32_t lastPollMs = 0;
#endif

    // Fires from sensor.update() when the fusion hub reports a wrist tilt.
    static void onWristTilt(uint8_t sensor_id, const uint8_t *data, uint32_t size, uint64_t *timestamp, void *user_data);

  public:
    explicit BHI260APSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
#ifdef MESHTASTIC_INCLUDE_INKHUD
    StepSource stepSource() const override { return stepCounterReady ? StepSource::Hardware : StepSource::None; }
    bool syncStepCounter() override { return stepCounterReady && pollSensor(); }
#endif
};

#endif

#endif
