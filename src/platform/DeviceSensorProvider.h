#pragma once

#include "detect/ScanI2C.h"

#include <memory>

class MotionSensor;
class TelemetrySensor;

// Sensor construction is selected by the device variant. Common motion-thread
// policy does not need to know which vendor driver owns a sensor.
class DeviceSensorProvider
{
  public:
    virtual ~DeviceSensorProvider() = default;
    virtual std::unique_ptr<MotionSensor> createAccelerometer(const ScanI2C::FoundDevice &device);
    virtual std::unique_ptr<TelemetrySensor> createEnvironmentalSensor(ScanI2C::DeviceType type);
};
