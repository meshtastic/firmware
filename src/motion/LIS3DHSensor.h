#pragma once
#ifndef _LIS3DH_SENSOR_H_
#define _LIS3DH_SENSOR_H_

#include "MotionSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include <Adafruit_LIS3DH.h>

class LIS3DHSensor : public MotionSensor
{
private:
  Adafruit_LIS3DH sensor;

public:
  LIS3DHSensor(ScanI2C::DeviceAddress address);
  virtual bool init() override;
  virtual int32_t runOnce() override;
};

#endif

#endif