#include "LSM6DS3Sensor.h"
#include "NodeDB.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

LSM6DS3Sensor::LSM6DS3Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool LSM6DS3Sensor::init()
{
    if (sensor.begin_I2C(deviceAddress())) {

        // Default threshold of 2G, less sensitive options are 4, 8 or 16G
        sensor.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);

        // Duration is number of occurrences needed to trigger, higher threshold is less sensitive
        sensor.enableWakeup(config.display.wake_on_tap_or_motion, 1, LSM6DS3_WAKE_THRESH);

        LOG_DEBUG("LSM6DS3 init ok");
        return true;
    }
    LOG_DEBUG("LSM6DS3 init failed");
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