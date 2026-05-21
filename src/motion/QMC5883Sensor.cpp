#include "QMC5883Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_QMC5883P.h>)

#include <Preferences.h>

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

QMC5883Sensor::QMC5883Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMC5883Sensor::init()
{
    // Select I2C bus based on detected port
    TwoWire *bus = &Wire;
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    bus = (devicePort() == ScanI2C::I2CPort::WIRE1) ? &Wire1 : &Wire;
#endif

    if (!sensor.begin(QMC5883P_ADDR, bus)) {
        LOG_DEBUG("QMC5883P init failed (chip ID mismatch or no response)");
        return false;
    }

    sensor.setMode(QMC5883P_MODE_CONTINUOUS);
    sensor.setODR(QMC5883P_ODR_50HZ);
    sensor.setRange(QMC5883P_RANGE_8G);

    // Load previously saved calibration from NVS
    loadCalibration();

    LOG_DEBUG("QMC5883P init ok");
    return true;
}

int32_t QMC5883Sensor::runOnce()
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    int16_t rawX = 0, rawY = 0, rawZ = 0;

    if (!sensor.getRawMagnetic(&rawX, &rawY, &rawZ)) {
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    // Log raw data every ~1 second (every 10th call at 100ms interval)
    {
        static uint8_t debugCounter = 0;
        if (++debugCounter >= 10) {
            debugCounter = 0;
            float cx = rawX - magOffsetX;
            float cy = rawY - magOffsetY;
            float h = atan2(cy, cx) * 180.0f / M_PI;
            if (h < 0)
                h += 360.0f;
            LOG_DEBUG("QMC5883P raw: X=%d Y=%d Z=%d heading=%.0f  cal=%.0f,%.0f", rawX, rawY, rawZ, h, magOffsetX, magOffsetY);
        }
    }

    if (doCalibration) {

        if (!showingScreen) {
            powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
            showingScreen = true;
            if (screen)
                screen->startAlert((FrameCallback)drawFrameCalibration);
        }

        if (rawX > highestX)
            highestX = rawX;
        if (rawX < lowestX)
            lowestX = rawX;
        if (rawY > highestY)
            highestY = rawY;
        if (rawY < lowestY)
            lowestY = rawY;
        if (rawZ > highestZ)
            highestZ = rawZ;
        if (rawZ < lowestZ)
            lowestZ = rawZ;

        uint32_t now = millis();
        if (now > endCalibrationAt) {
            doCalibration = false;
            endCalibrationAt = 0;
            showingScreen = false;
            if (screen)
                screen->endAlert();

            // Compute and persist hard-iron offsets
            magOffsetX = (highestX + lowestX) / 2.0f;
            magOffsetY = (highestY + lowestY) / 2.0f;
            magOffsetZ = (highestZ + lowestZ) / 2.0f;
            saveCalibration();

            LOG_DEBUG("QMC5883P calibration done: X=[%.0f..%.0f] Y=[%.0f..%.0f] Z=[%.0f..%.0f]", lowestX, highestX, lowestY,
                      highestY, lowestZ, highestZ);
        }
    }

    // Apply hard-iron calibration offset to X and Y
    float cx = rawX - magOffsetX;
    float cy = rawY - magOffsetY;

    float heading = atan2(cy, cx) * 180.0f / M_PI;
    if (heading < 0)
        heading += 360.0f;

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
    int16_t rawX = 0, rawY = 0, rawZ = 0;
    sensor.getRawMagnetic(&rawX, &rawY, &rawZ);
    highestX = rawX;
    lowestX = rawX;
    highestY = rawY;
    lowestY = rawY;
    highestZ = rawZ;
    lowestZ = rawZ;

    LOG_DEBUG("QMC5883P calibration start values: X=%d Y=%d Z=%d", rawX, rawY, rawZ);

    doCalibration = true;
    uint16_t calibrateFor = forSeconds * 1000; // calibrate for seconds provided
    endCalibrationAt = millis() + calibrateFor;
    if (screen)
        screen->setEndCalibration(endCalibrationAt);

    LOG_DEBUG("QMC5883P calibration started for %is", forSeconds);
#endif
}

void QMC5883Sensor::loadCalibration()
{
#ifdef ARCH_ESP32
    Preferences prefs;
    prefs.begin("meshtastic", true); // read-only mode
    magOffsetX = prefs.getFloat("qmcOffX", 0);
    magOffsetY = prefs.getFloat("qmcOffY", 0);
    magOffsetZ = prefs.getFloat("qmcOffZ", 0);
    prefs.end();

    if (magOffsetX != 0 || magOffsetY != 0 || magOffsetZ != 0) {
        LOG_DEBUG("QMC5883P loaded calibration: offset=(%.0f,%.0f,%.0f)", magOffsetX, magOffsetY, magOffsetZ);
    }
#endif
}

void QMC5883Sensor::saveCalibration()
{
#ifdef ARCH_ESP32
    Preferences prefs;
    prefs.begin("meshtastic", false); // read-write mode
    prefs.putFloat("qmcOffX", magOffsetX);
    prefs.putFloat("qmcOffY", magOffsetY);
    prefs.putFloat("qmcOffZ", magOffsetZ);
    prefs.end();
    LOG_DEBUG("QMC5883P saved calibration: offset=(%.0f,%.0f,%.0f)", magOffsetX, magOffsetY, magOffsetZ);
#endif
}

#endif
