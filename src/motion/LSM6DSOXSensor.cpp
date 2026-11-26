#include "LSM6DSOXSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && !MESHTASTIC_EXCLUDE_ACCELEROMETER && __has_include(<Adafruit_LSM6DSOX.h>)

#include "detect/ScanI2CTwoWire.h"

bool LSM6DSOXSensor::Sensor::configureWakeup(bool enable)
{
    Adafruit_BusIO_Register tapCfg0(i2c_dev, 0x56);
    Adafruit_BusIO_RegisterBits slopeFilter(&tapCfg0, 1, 4);
    Adafruit_BusIO_Register tapCfg2(i2c_dev, 0x58);
    Adafruit_BusIO_RegisterBits interruptsEnable(&tapCfg2, 1, 7);
    Adafruit_BusIO_Register wakeDuration(i2c_dev, 0x5C);
    Adafruit_BusIO_RegisterBits duration(&wakeDuration, 2, 5);
    Adafruit_BusIO_Register wakeThreshold(i2c_dev, 0x5B);
    Adafruit_BusIO_RegisterBits threshold(&wakeThreshold, 6, 0);

    return slopeFilter.write(0) && duration.write(1) && threshold.write(LSM6DSOX_WAKE_THRESH) && interruptsEnable.write(enable);
}

LSM6DSOXSensor::LSM6DSOXSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor(foundDevice) {}

bool LSM6DSOXSensor::init()
{
    if (!sensor.begin_I2C(deviceAddress(), ScanI2CTwoWire::fetchI2CBus(device.address))) {
        LOG_WARN("LSM6DSOX init failed");
        return false;
    }

    sensor.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
    if (!sensor.configureWakeup(config.display.wake_on_tap_or_motion)) {
        LOG_WARN("LSM6DSOX wake configuration failed");
        return false;
    }

    LOG_DEBUG("LSM6DSOX init ok");
    return true;
}

int32_t LSM6DSOXSensor::runOnce()
{
    if (config.display.wake_on_tap_or_motion && sensor.awake()) {
        wakeScreen();
        return 500;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif