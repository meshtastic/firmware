#include "BMX160Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

BMX160Sensor::BMX160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

#ifdef RAK_4631
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

bool BMX160Sensor::init()
{
    if (sensor.begin()) {
        // set output data rate
        sensor.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ);

        loadState();

        LOG_INFO("BMX160 load calibration min_x: %.4f, max_X: %.4f, min_Y: %.4f, max_Y: %.4f, min_Z: %.4f, max_Z: %.4f",
                 sensorConfig.mAccel.min.x, sensorConfig.mAccel.max.x, sensorConfig.mAccel.min.y, sensorConfig.mAccel.max.y,
                 sensorConfig.mAccel.min.z, sensorConfig.mAccel.max.z);

        return true;
    }
    LOG_DEBUG("BMX160 init failed");
    return false;
}

int32_t BMX160Sensor::runOnce()
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    sBmx160SensorData_t magAccel;
    sBmx160SensorData_t gyroAccel;
    sBmx160SensorData_t gAccel;

    /* Get a new sensor event */
    sensor.getAllData(&magAccel, &gyroAccel, &gAccel);

    if (doMagCalibration) {
        getMagCalibrationData(magAccel.x, magAccel.y, magAccel.z);
    } else if (doGyroWarning) {
        gyroCalibrationWarning();
    } else if (doGyroCalibration) {
        getGyroCalibrationData(gyroAccel.x, gyroAccel.y, gyroAccel.z, gAccel.x, gAccel.y, gAccel.z);
    }

    int highestRealX = sensorConfig.mAccel.max.x - (sensorConfig.mAccel.max.x + sensorConfig.mAccel.min.x) / 2;

    magAccel.x -= (sensorConfig.mAccel.max.x + sensorConfig.mAccel.min.x) / 2;
    magAccel.y -= (sensorConfig.mAccel.max.y + sensorConfig.mAccel.min.y) / 2;
    magAccel.z -= (sensorConfig.mAccel.max.z + sensorConfig.mAccel.min.z) / 2;
    FusionVector ga, ma;
    ga.axis.x = gAccel.x * sensorConfig.orientation.x; // default location for the BMX160 is on the rear of the board
    ga.axis.y = gAccel.y * sensorConfig.orientation.y;
    ga.axis.z = gAccel.z * sensorConfig.orientation.z;
    ma.axis.x = magAccel.x * sensorConfig.orientation.x;
    ma.axis.y = magAccel.y * sensorConfig.orientation.y;
    ma.axis.z = magAccel.z * sensorConfig.orientation.z * 3;

    // If we're set to one of the inverted positions
    if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
        ma = FusionAxesSwap(ma, FusionAxesAlignmentNXNYPZ);
        ga = FusionAxesSwap(ga, FusionAxesAlignmentNXNYPZ);
    }

    float heading = FusionCompassCalculateHeading(FusionConventionNed, ga, ma);

    switch (config.display.compass_orientation) {
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0_INVERTED:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0:
        break;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90_INVERTED:
        heading += 90;
        break;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180_INVERTED:
        heading += 180;
        break;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270_INVERTED:
        heading += 270;
        break;
    }

    screen->setHeading(heading);
#endif

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

void BMX160Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    LOG_INFO("BMX160 calibration started for %is", forSeconds);

    doMagCalibration = true;
    firstCalibrationRead = true;
    uint16_t calibrateFor = forSeconds * 1000; // calibrate for seconds provided
    endMagCalibrationAt = millis() + calibrateFor;
    screen->setEndCalibration(endMagCalibrationAt);
#endif
}

#endif

#endif