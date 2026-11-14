#pragma once

#include "configuration.h"

#if defined(MESHTASTIC_INCLUDE_INKHUD) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C &&                                   \
    !MESHTASTIC_EXCLUDE_ACCELEROMETER && __has_include(<Adafruit_ISM330DHCX.h>)

#include "MotionSensor.h"
#include "detect/ScanI2CTwoWire.h"
#include <Adafruit_ISM330DHCX.h>

class ISM330DHCXSensor : public MotionSensor
{
  public:
    explicit ISM330DHCXSensor(ScanI2C::FoundDevice device) : MotionSensor(device) {}
    bool init() override { return sensor.begin_I2C(deviceAddress(), ScanI2CTwoWire::fetchI2CBus(device.address)); }
    StepSource stepSource() const override { return StepSource::Software; }

    int32_t runOnce() override
    {
        if (stepTracker().isTracking()) {
            float x, y, z;
            if (sensor.readAcceleration(x, y, z))
                sampleSteps(x, y, z);
        }
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

  private:
    Adafruit_ISM330DHCX sensor;
};

#endif
