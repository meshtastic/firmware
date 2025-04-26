#include "BMX160Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

BMX160Sensor::BMX160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

#if defined(RAK_4631) && !defined(RAK2560) && __has_include(<Rak_BMX160.h>)
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

bool BMX160Sensor::init()
{
    if (sensor.begin()) {
        // set output data rate
        sensor.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ);
        LOG_DEBUG("BMX160 init ok");
        return true;
    }
    LOG_DEBUG("BMX160 init failed");
    return false;
}

int32_t BMX160Sensor::runOnce()
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    sBmx160SensorData_t magAccel;
    sBmx160SensorData_t gAccel;

    /* Get a new sensor event */
    sensor.getAllData(&magAccel, NULL, &gAccel);

    if (doCalibration) {

        if (!showingScreen) {
            powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
            showingScreen = true;
            screen->startAlert((FrameCallback)drawFrameCalibration);
        }

        if (magAccel.x > highestX)
            highestX = magAccel.x;
        if (magAccel.x < lowestX)
            lowestX = magAccel.x;
        if (magAccel.y > highestY)
            highestY = magAccel.y;
        if (magAccel.y < lowestY)
            lowestY = magAccel.y;
        if (magAccel.z > highestZ)
            highestZ = magAccel.z;
        if (magAccel.z < lowestZ)
            lowestZ = magAccel.z;

        uint32_t now = millis();
        if (now > endCalibrationAt) {
            doCalibration = false;
            endCalibrationAt = 0;
            showingScreen = false;
            screen->endAlert();
        }

        // LOG_DEBUG("BMX160 min_x: %.4f, max_X: %.4f, min_Y: %.4f, max_Y: %.4f, min_Z: %.4f, max_Z: %.4f", lowestX, highestX,
        // lowestY, highestY, lowestZ, highestZ);
    }

    int highestRealX = highestX - (highestX + lowestX) / 2;

    magAccel.x -= (highestX + lowestX) / 2;
    magAccel.y -= (highestY + lowestY) / 2;
    magAccel.z -= (highestZ + lowestZ) / 2;
    FusionVector ga, ma;
    ga.axis.x = -gAccel.x; // default location for the BMX160 is on the rear of the board
    ga.axis.y = -gAccel.y;
    ga.axis.z = gAccel.z;
    ma.axis.x = -magAccel.x;
    ma.axis.y = -magAccel.y;
    ma.axis.z = magAccel.z * 3;

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
    LOG_DEBUG("BMX160 calibration started for %is", forSeconds);

    doCalibration = true;
    uint16_t calibrateFor = forSeconds * 1000; // calibrate for seconds provided
    endCalibrationAt = millis() + calibrateFor;
    screen->setEndCalibration(endCalibrationAt);
#endif
}

#endif

#endif