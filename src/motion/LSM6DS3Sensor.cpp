#include "LSM6DS3Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

LSM6DS3Sensor::LSM6DS3Sensor(ScanI2C::DeviceAddress address) : MotionSensor::MotionSensor(ScanI2C::DeviceType::LSM6DS3, address)
{
}

bool LSM6DS3Sensor::init()
{
    if (sensor.begin_I2C(deviceAddress())) {

        // Default threshold of 2G, less sensitive options are 4, 8 or 16G
        sensor.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);

        // Duration is number of occurances needed to trigger, higher threshold is less sensitive
        sensor.enableWakeup(config.display.wake_on_tap_or_motion, 1, LSM6DS3_WAKE_THRESH);

        LOG_DEBUG("LSM6DS3Sensor::init ok\n");
        return true;
    }
    LOG_DEBUG("LSM6DS3Sensor::init failed\n");
    return false;
}

int32_t LSM6DS3Sensor::runOnce()
{
    if (sensor.shake()) {
        wakeScreen();
        return 500;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif