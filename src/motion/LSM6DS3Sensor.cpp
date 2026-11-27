#include "LSM6DS3Sensor.h"
#include "NodeDB.h"
#include "Throttle.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_LSM6DS3TRC.h>)

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
#ifdef MESHTASTIC_INCLUDE_INKHUD
    if (stepTracker().isTracking()) {
        float x, y, z;
        if (sensor.readAcceleration(x, y, z))
            sampleSteps(x, y, z);
    }
#endif
#if HAS_SCREEN_AUTO_ROTATE
    static uint32_t lastOrientationRead = 0;
    if (Throttle::hasElapsed(lastOrientationRead, 100)) {
        lastOrientationRead = millis();
        float x, y, z;
        if (sensor.readAcceleration(x, y, z))
            followOrientation(x, y, z);
    }
    // The orientation keeps its 100 ms samples: a shake wakes the screen at most every 500 ms instead of pausing them
    static uint32_t lastShakeWake = 0;
    static bool shakeWoke = false;
    if (sensor.shake() && (!shakeWoke || Throttle::hasElapsed(lastShakeWake, 500))) {
        wakeScreen();
        lastShakeWake = millis();
        shakeWoke = true;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
#else
    if (sensor.shake()) {
        wakeScreen();
        return 500;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
#endif
}

#endif
