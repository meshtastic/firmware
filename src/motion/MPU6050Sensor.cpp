#include "MPU6050Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

MPU6050Sensor::MPU6050Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool MPU6050Sensor::init()
{
    if (sensor.begin(deviceAddress())) {
        // setup motion detection
        sensor.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
        sensor.setMotionDetectionThreshold(1);
        sensor.setMotionDetectionDuration(20);
        sensor.setInterruptPinLatch(true); // Keep it latched.  Will turn off when reinitialized.
        sensor.setInterruptPinPolarity(true);
        LOG_DEBUG("MPU6050 init ok");
        return true;
    }
    LOG_DEBUG("MPU6050 init failed");
    return false;
}

int32_t MPU6050Sensor::runOnce()
{
    if (sensor.getMotionInterruptStatus()) {
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif