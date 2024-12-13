#include "BMX160Sensor.h"
#include "FSCommon.h"

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

        LOG_DEBUG("BMX160 min_x: %.4f, max_X: %.4f, min_Y: %.4f, max_Y: %.4f, min_Z: %.4f, max_Z: %.4f",
                  bmx160Config.mAccel.min.x, bmx160Config.mAccel.max.x, bmx160Config.mAccel.min.y, bmx160Config.mAccel.max.y,
                  bmx160Config.mAccel.min.z, bmx160Config.mAccel.max.z);

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

        if (magAccel.x > bmx160Config.mAccel.max.x)
            bmx160Config.mAccel.max.x = magAccel.x;
        if (magAccel.x < bmx160Config.mAccel.min.x)
            bmx160Config.mAccel.min.x = magAccel.x;
        if (magAccel.y > bmx160Config.mAccel.max.y)
            bmx160Config.mAccel.max.y = magAccel.y;
        if (magAccel.y < bmx160Config.mAccel.min.y)
            bmx160Config.mAccel.min.y = magAccel.y;
        if (magAccel.z > bmx160Config.mAccel.max.z)
            bmx160Config.mAccel.max.z = magAccel.z;
        if (magAccel.z < bmx160Config.mAccel.min.z)
            bmx160Config.mAccel.min.z = magAccel.z;

        uint32_t now = millis();
        if (now > endCalibrationAt) {
            doCalibration = false;
            endCalibrationAt = 0;
            showingScreen = false;
            screen->endAlert();

            updateState();
        }

        // LOG_DEBUG("BMX160 min_x: %.4f, max_X: %.4f, min_Y: %.4f, max_Y: %.4f, min_Z: %.4f, max_Z: %.4f", lowestX, highestX,
        // lowestY, highestY, lowestZ, highestZ);
    }

    int highestRealX = bmx160Config.mAccel.max.x - (bmx160Config.mAccel.max.x + bmx160Config.mAccel.min.x) / 2;

    magAccel.x -= (bmx160Config.mAccel.max.x + bmx160Config.mAccel.min.x) / 2;
    magAccel.y -= (bmx160Config.mAccel.max.y + bmx160Config.mAccel.min.y) / 2;
    magAccel.z -= (bmx160Config.mAccel.max.z + bmx160Config.mAccel.min.z) / 2;
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

void BMX160Sensor::loadState()
{
#ifdef FSCom
    auto file = FSCom.open(bmx160ConfigFileName, FILE_O_READ);
    if (file) {
        file.read((uint8_t *)&bmx160State, BMX160_MAX_STATE_BLOB_SIZE);
        file.close();

        memcpy(&bmx160Config, &bmx160State, sizeof(BMX160Config));

        LOG_INFO("BMX160 config state read from %s", bmx160ConfigFileName);
    } else {
        LOG_INFO("No BMX160 config state found (File: %s)", bmx160ConfigFileName);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
}

void BMX160Sensor::updateState()
{
#ifdef FSCom
    memcpy(&bmx160State, &bmx160Config, sizeof(BMX160Config));

    LOG_DEBUG("BMX160 min_x: %.4f, max_X: %.4f, min_Y: %.4f, max_Y: %.4f, min_Z: %.4f, max_Z: %.4f", bmx160Config.mAccel.min.x,
              bmx160Config.mAccel.max.x, bmx160Config.mAccel.min.y, bmx160Config.mAccel.max.y, bmx160Config.mAccel.min.z,
              bmx160Config.mAccel.max.z);

    if (FSCom.exists(bmx160ConfigFileName) && !FSCom.remove(bmx160ConfigFileName)) {
        LOG_WARN("Can't remove old state file");
    }
    auto file = FSCom.open(bmx160ConfigFileName, FILE_O_WRITE);
    if (file) {
        LOG_INFO("Write BMX160 config state to %s", bmx160ConfigFileName);
        file.write((uint8_t *)&bmx160State, BMX160_MAX_STATE_BLOB_SIZE);
        file.flush();
        file.close();
    } else {
        LOG_INFO("Can't write BMX160 config state (File: %s)", bmx160ConfigFileName);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
}

#endif

#endif