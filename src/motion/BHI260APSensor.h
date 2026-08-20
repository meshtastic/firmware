#pragma once
#ifndef _BHI260AP_SENSOR_H_
#define _BHI260AP_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(T_DECK_MAX) && defined(HAS_BHI260AP) && \
    __has_include(<SensorBHI260AP.hpp>)

#include <SensorBHI260AP.hpp>

class BHI260APSensor : public MotionSensor
{
  public:
    explicit BHI260APSensor(ScanI2C::FoundDevice foundDevice);
    ~BHI260APSensor() override;

    bool init() override;
    int32_t runOnce() override;

  private:
    static void accelerationCallback(uint8_t sensorId, uint8_t *data, uint32_t length, uint64_t *timestamp,
                                     void *userData);
    static void dataReadyISR();
    void processAcceleration(uint8_t sensorId, uint8_t *data, uint32_t length);
    void releaseResources();

    SensorBHI260AP sensor;
    static BHI260APSensor *interruptInstance;
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

#endif

#endif
