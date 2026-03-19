#include "LSM6DSOSensor.h"
#include "NodeDB.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_LSM6DSOX.h>)

LSM6DSOSensor::LSM6DSOSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool LSM6DSOSensor::init()
{
    if (sensor.begin_I2C(deviceAddress())) {

        sensor.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
        sensor.enableWakeup(config.display.wake_on_tap_or_motion, 1, LSM6DSO_WAKE_THRESH);

        LOG_DEBUG("LSM6DSO init ok");
        return true;
    }
    LOG_DEBUG("LSM6DSO init failed");
    return false;
}

int32_t LSM6DSOSensor::runOnce()
{
    if (sensor.shake()) {
        wakeScreen();
        return 500;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
