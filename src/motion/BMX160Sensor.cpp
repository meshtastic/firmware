#include "BMX160Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

BMX160Sensor::BMX160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

#if !defined(RAK2560) && __has_include(<Rak_BMX160.h>)
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern std::unique_ptr<graphics::Screen> screen;
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

    // Smooth raw inputs with a per-axis EMA to suppress dynamic acceleration
    // noise during rotation (stateless FusionCompass turns it into heading jitter).
    FusionVector accel = {{gAccel.x, gAccel.y, gAccel.z}};
    FusionVector mag = {{magAccel.x, magAccel.y, magAccel.z}};
    if (!filtersSeeded) {
        accelFiltered = accel;
        magFiltered = mag;
        filtersSeeded = true;
    } else {
        for (int i = 0; i < 3; ++i) {
            accelFiltered.array[i] = accelFilterAlpha * accel.array[i] + (1.0f - accelFilterAlpha) * accelFiltered.array[i];
            magFiltered.array[i] = magFilterAlpha * mag.array[i] + (1.0f - magFilterAlpha) * magFiltered.array[i];
        }
    }
    accel = accelFiltered;
    mag = magFiltered;

    FusionVector ga, ma;
    ga.axis.x = -accel.axis.x; // default location for the BMX160 is on the rear of the board
    ga.axis.y = -accel.axis.y;
    ga.axis.z = accel.axis.z;
    ma.axis.x = -mag.axis.x;
    ma.axis.y = -mag.axis.y;
    ma.axis.z = mag.axis.z * 3;

    // Compensate for non-flat case mounting. FusionCompass() assumes chip Z is
    // world-up; on a vertical mount (LCD perpendicular to the board) chip Z is
    // horizontal and the tilt-comp math becomes unstable. Override which chip
    // axis is treated as world-up via the BMX160_UP_AXIS_* defines.
#ifndef BMX160_UP_AXIS
#define BMX160_UP_AXIS BMX160_UP_AXIS_PZ
#endif
#if BMX160_UP_AXIS == BMX160_UP_AXIS_PX
    ga = FusionRemap(ga, FusionRemapAlignmentNZPYPX);
    ma = FusionRemap(ma, FusionRemapAlignmentNZPYPX);
#elif BMX160_UP_AXIS == BMX160_UP_AXIS_PZ
    // Default orientation (chip +Z up), no remap needed.
#elif BMX160_UP_AXIS == BMX160_UP_AXIS_NX
    ga = FusionRemap(ga, FusionRemapAlignmentPZPYNX);
    ma = FusionRemap(ma, FusionRemapAlignmentPZPYNX);
#elif BMX160_UP_AXIS == BMX160_UP_AXIS_PY
    ga = FusionRemap(ga, FusionRemapAlignmentPXNZPY);
    ma = FusionRemap(ma, FusionRemapAlignmentPXNZPY);
#elif BMX160_UP_AXIS == BMX160_UP_AXIS_NY
    ga = FusionRemap(ga, FusionRemapAlignmentPXPZNY);
    ma = FusionRemap(ma, FusionRemapAlignmentPXPZNY);
#else
#error "BMX160_UP_AXIS must be one of BMX160_UP_AXIS_PZ/PX/NX/PY/NY"
#endif

    // If we're set to one of the inverted positions
    if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
        ma = FusionRemap(ma, FusionRemapAlignmentNXNYPZ);
        ga = FusionRemap(ga, FusionRemapAlignmentNXNYPZ);
    }

    float heading = FusionCompass(ga, ma, FusionConventionNed);

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
