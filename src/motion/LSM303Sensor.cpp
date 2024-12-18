#include "LSM303Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

LSM303Sensor::LSM303Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool LSM303Sensor::init()
{
    if (sensor.begin()) {
        // setup motion detection
        sensor.setRange(LSM303_RANGE_4G);
        sensor.setMode(LSM303_MODE_LOW_POWER);
        LOG_DEBUG("LSM303 init ok");
        return true;
    }
    LOG_DEBUG("LSM303 init failed");
    return false;
}

int32_t LSM303Sensor::runOnce()
{
    // sensors_event_t event;
    // if (sensor.getEvent(&event)) {
    //     wakeScreen();
    // }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif