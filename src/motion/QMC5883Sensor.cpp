#include "QMC5883Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<QMC5883LCompass.h>)
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

QMC5883Sensor::QMC5883Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMC5883Sensor::init()
{
    sensor.init();
    sensor.setSmoothing(5, false); // 5-sample rolling average for stability
    LOG_DEBUG("QMC5883L init ok");
    return true;
}

int32_t QMC5883Sensor::runOnce()
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    sensor.read();
    int magX = sensor.getX();
    int magY = sensor.getY();
    int magZ = sensor.getZ();

    if (doCalibration) {

        if (!showingScreen) {
            powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
            showingScreen = true;
            if (screen)
                screen->startAlert((FrameCallback)drawFrameCalibration);
        }

        if (magX > highestX)
            highestX = magX;
        if (magX < lowestX)
            lowestX = magX;
        if (magY > highestY)
            highestY = magY;
        if (magY < lowestY)
            lowestY = magY;
        if (magZ > highestZ)
            highestZ = magZ;
        if (magZ < lowestZ)
            lowestZ = magZ;

        uint32_t now = millis();
        if (now > endCalibrationAt) {
            doCalibration = false;
            endCalibrationAt = 0;
            showingScreen = false;
            if (screen)
                screen->endAlert();

            // Compute hard-iron offset (bias) from min/max
            float x_offset = (highestX + lowestX) / 2.0f;
            float y_offset = (highestY + lowestY) / 2.0f;
            float z_offset = (highestZ + lowestZ) / 2.0f;

            // Compute soft-iron scale factors
            float x_avg_delta = (highestX - lowestX) / 2.0f;
            float y_avg_delta = (highestY - lowestY) / 2.0f;
            float z_avg_delta = (highestZ - lowestZ) / 2.0f;
            float avg_delta = (x_avg_delta + y_avg_delta + z_avg_delta) / 3.0f;

            // Avoid division by zero
            if (x_avg_delta < 1.0f)
                x_avg_delta = 1.0f;
            if (y_avg_delta < 1.0f)
                y_avg_delta = 1.0f;
            if (z_avg_delta < 1.0f)
                z_avg_delta = 1.0f;

            sensor.setCalibrationOffsets(x_offset, y_offset, z_offset);
            sensor.setCalibrationScales(avg_delta / x_avg_delta, avg_delta / y_avg_delta, avg_delta / z_avg_delta);

            LOG_DEBUG("QMC5883L calibration applied: offset=(%.0f,%.0f,%.0f) scale=(%.2f,%.2f,%.2f)", x_offset, y_offset,
                      z_offset, avg_delta / x_avg_delta, avg_delta / y_avg_delta, avg_delta / z_avg_delta);
        }
    }

    float heading = sensor.getAzimuth();

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
    if (screen)
        screen->setHeading(heading);
#endif

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

void QMC5883Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    sensor.read();
    sensor.clearCalibration();
    highestX = sensor.getX();
    lowestX = sensor.getX();
    highestY = sensor.getY();
    lowestY = sensor.getY();
    highestZ = sensor.getZ();
    lowestZ = sensor.getZ();

    doCalibration = true;
    uint16_t calibrateFor = forSeconds * 1000; // calibrate for seconds provided
    endCalibrationAt = millis() + calibrateFor;
    if (screen)
        screen->setEndCalibration(endCalibrationAt);

    LOG_DEBUG("QMC5883L calibration started for %is", forSeconds);
#endif
}

#endif
