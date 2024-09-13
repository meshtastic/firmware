#pragma once
#ifndef _LSM6DS3_SENSOR_H_
#define _LSM6DS3_SENSOR_H_

#include "MotionSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#ifndef LSM6DS3_WAKE_THRESH
#define LSM6DS3_WAKE_THRESH 20
#endif

#include <Adafruit_LSM6DS3TRC.h>

class LSM6DS3Sensor : public MotionSensor
{
  private:
    Adafruit_LSM6DS3TRC sensor;

  public:
    LSM6DS3Sensor(ScanI2C::DeviceAddress address);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif