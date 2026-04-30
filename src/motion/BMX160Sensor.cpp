#include "BMX160Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

BMX160Sensor::BMX160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

#if !defined(RAK2560) && __has_include(<Rak_BMX160.h>)
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

bool BMX160Sensor::init()
{
    if (sensor.begin()) {
        // set output data rate
        sensor.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ);
        loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
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
        beginCalibrationDisplay(showingScreen);
        updateCalibrationExtrema(magAccel.x, magAccel.y, magAccel.z, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }

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

    heading = applyCompassOrientation(heading);
    if (screen)
        screen->setHeading(heading);
#endif

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

void BMX160Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    sBmx160SensorData_t magAccel;
    sBmx160SensorData_t gAccel;
    LOG_DEBUG("BMX160 calibration started for %is", forSeconds);
    sensor.getAllData(&magAccel, NULL, &gAccel);
    seedCalibrationExtrema(magAccel.x, magAccel.y, magAccel.z, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    startCalibrationWindow(forSeconds);
#endif
}

#endif

#endif
