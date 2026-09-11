#pragma once

#include "motion/MotionSensor.h"

#include <SensorBHI260AP.hpp>
#include <Wire.h>
#include <bosch/BoschSensorDataHelper.hpp>

class TDeckMaxBHI260APSensor final : public MotionSensor
{
  public:
    explicit TDeckMaxBHI260APSensor(ScanI2C::FoundDevice foundDevice);
    ~TDeckMaxBHI260APSensor() override;

    bool init() override;
    int32_t runOnce() override;

  private:
    static void accelerationCallback(uint8_t sensorId, uint8_t *data, uint32_t length, uint64_t *timestamp,
                                     void *userData);
    static void dataReadyISR();
    void processAcceleration(uint8_t sensorId, uint8_t *data, uint32_t length);
    void releaseResources();

    SensorBHI260AP sensor;
    static TDeckMaxBHI260APSensor *interruptInstance;
    volatile bool motionDetected = false;
    volatile bool dataReadyPending = false;
    bool initialized = false;
    bool callbackRegistered = false;
    bool irqAttached = false;
    bool hasBaseline = false;
    float baselineX = 0.0f;
    float baselineY = 0.0f;
    float baselineZ = 0.0f;
};
