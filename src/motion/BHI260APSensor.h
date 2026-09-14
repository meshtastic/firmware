#pragma once
#ifndef _BHI260AP_SENSOR_H_
#define _BHI260AP_SENSOR_H_

#include "MotionSensor.h"
#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && __has_include(<SensorBHI260AP.hpp>)

#include <SensorBHI260AP.hpp>
#include <Wire.h>
#include <bosch/BoschSensorDataHelper.hpp>

class BHI260APSensor : public MotionSensor
{
  private:
    SensorBHI260AP sensor;
    SensorStepCounter *stepCounter = nullptr;
    SensorStepDetector *stepDetector = nullptr;
    SensorQuaternion *gameRotation = nullptr;
    SensorXYZ *linearAcceleration = nullptr;
    uint32_t steps = 0;
    bool wakeRequested = false;
#ifdef BHI260AP_INT
    uint32_t lastPollMs = 0;
#endif

    static volatile float latestYawDeg;
    static volatile float latestLinearX;
    static volatile float latestLinearY;
    static volatile float latestLinearZ;
    static volatile uint32_t latestYawMs;
    static volatile uint32_t latestLinearMs;
    static volatile float gnssSpeedAnchorKmph;
    static volatile float speedEstimateKmph;
    static volatile uint32_t speedAnchorMs;
    static volatile uint32_t speedIntegrationMs;
    static volatile bool speedAnchorValid;

    static void onWristTilt(uint8_t sensor_id, const uint8_t *data, uint32_t size, uint64_t *timestamp, void *user_data);

  public:
    explicit BHI260APSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;

    // Short-lived IMU-assist data. These values never replace GNSS as the
    // absolute reference; Screen/GPS use them only between fresh L76K samples.
    static bool getLatestYawDegrees(float &yawDeg, uint32_t &ageMs);
    static bool getLatestLinearAcceleration(float &x, float &y, float &z, uint32_t &ageMs);
    static bool getForwardAcceleration(float &accelMps2, uint32_t &ageMs);

    // L76K always owns the absolute speed.  These methods only maintain a
    // bounded 3 s dead-reckoning bridge from the most recent GNSS speed.
    static void setGnssSpeedAnchor(float speedKmph);
    static bool getBridgedSpeedKmph(float &speedKmph, uint32_t &anchorAgeMs);
};
#endif

#endif
