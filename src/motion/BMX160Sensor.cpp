#include "BMX160Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

BMX160Sensor::BMX160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

#ifdef RAK_4631

// screen is defined in main.cpp
extern graphics::Screen *screen;

bool BMX160Sensor::init()
{
    if (sensor.begin()) {
        // set output data rate
        sensor.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ);
        LOG_DEBUG("BMX160Sensor::init ok\n");
        return true;
    }
    LOG_DEBUG("BMX160Sensor::init failed\n");
    return false;
}

int32_t BMX160Sensor::runOnce()
{
    sBmx160SensorData_t magAccel;
    sBmx160SensorData_t gAccel;

    /* Get a new sensor event */
    sensor.getAllData(&magAccel, NULL, &gAccel);

    // experimental calibrate routine. Limited to between 10 and 30 seconds after boot
    if (millis() > 12 * 1000 && millis() < 30 * 1000) {
        if (!showingScreen) {
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
    } else if (showingScreen && millis() >= 30 * 1000) {
        showingScreen = false;
        screen->endAlert();
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

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif

#endif