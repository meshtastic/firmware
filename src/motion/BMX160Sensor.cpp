#include "BMX160Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

BMX160Sensor::BMX160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

#if defined(RAK_4631) && !defined(RAK2560)
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

bool BMX160Sensor::init()
{
    if (sensor.begin()) {
        // set output data rate
        sensor.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ);
        sensor.setGyroRange(eGyroRange_500DPS);
        sensor.setAccelRange(eAccelRange_2G);

        // default location for the BMX160 is on the rear of the board with Z negative
        sensorConfig.orientation.x = -1;
        sensorConfig.orientation.y = -1;
        sensorConfig.orientation.z = 1;

        loadState();

        LOG_INFO("BMX160 MAG calibration center_x: %.4f, center_Y: %.4f, center_Z: %.4f", sensorConfig.mAccel.x,
                 sensorConfig.mAccel.y, sensorConfig.mAccel.z);
        LOG_INFO("BMX160 GYRO calibration center_x: %.4f, center_Y: %.4f, center_Z: %.4f", sensorConfig.gyroAccel.x,
                 sensorConfig.gyroAccel.y, sensorConfig.gyroAccel.z);
        LOG_INFO("BMX160 ORIENT calibration: x=%i, y=%i, z=%i", sensorConfig.orientation.x, sensorConfig.orientation.y,
                 sensorConfig.orientation.z);

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

    // int highestRealX = sensorConfig.mAccel.max.x - (sensorConfig.mAccel.max.x + sensorConfig.mAccel.min.x) / 2;

    magAccel.x -= sensorConfig.mAccel.x;
    magAccel.y -= sensorConfig.mAccel.y;
    magAccel.z -= sensorConfig.mAccel.z;

    FusionVector ga, ma;
    ga.axis.x = gAccel.x * sensorConfig.orientation.x;
    ga.axis.y = gAccel.y * sensorConfig.orientation.y;
    ga.axis.z = gAccel.z * sensorConfig.orientation.z;
    ma.axis.x = magAccel.x * sensorConfig.orientation.x;
    ma.axis.y = magAccel.y * sensorConfig.orientation.y;
    ma.axis.z = magAccel.z * sensorConfig.orientation.z * 3;

    // Use calibration orientation instead of swap based on CompassOrientation definition
    // If we're set to one of the inverted positions
    // if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
    //     ma = FusionAxesSwap(ma, FusionAxesAlignmentNXNYPZ);
    //     ga = FusionAxesSwap(ga, FusionAxesAlignmentNXNYPZ);
    // }

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

    // LOG_DEBUG("MAG Sensor Data: X=%.4f, Y=%.4f, Z=%.4f", magAccel.x, magAccel.y, magAccel.z);
    // LOG_DEBUG("ACCEL Sensor Data: X=%.4f, Y=%.4f, Z=%.4f", gAccel.x, gAccel.y, gAccel.z);
    // LOG_DEBUG("HEADING Sensor Data: %.1f deg", heading);
    // LOG_DEBUG("Gyro Sensor Data: X=%.4f, Y=%.4f, Z=%.4f", gyroAccel.x, gyroAccel.y, gyroAccel.z);

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