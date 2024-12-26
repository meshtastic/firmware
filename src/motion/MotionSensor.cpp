#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

char timeRemainingBuffer[20];

// screen is defined in main.cpp
extern graphics::Screen *screen;

MotionSensor::MotionSensor(ScanI2C::FoundDevice foundDevice)
{
    device.address.address = foundDevice.address.address;
    device.address.port = foundDevice.address.port;
    device.type = foundDevice.type;
    LOG_DEBUG("Motion MotionSensor port: %s address: 0x%x type: %d", devicePort() == ScanI2C::I2CPort::WIRE1 ? "Wire1" : "Wire",
              (uint8_t)deviceAddress(), deviceType());
}

ScanI2C::DeviceType MotionSensor::deviceType()
{
    return device.type;
}

uint8_t MotionSensor::deviceAddress()
{
    return device.address.address;
}

ScanI2C::I2CPort MotionSensor::devicePort()
{
    return device.address.port;
}

#if defined(RAK_4631) & !MESHTASTIC_EXCLUDE_SCREEN
void MotionSensor::drawFrameCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // int x_offset = display->width() / 2;
    // int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Calibrating\nCompass");

    uint8_t timeRemaining = (screen->getEndCalibration() - millis()) / 1000;
    sprintf(timeRemainingBuffer, "( %02d )", timeRemaining);
    display->setFont(FONT_SMALL);
    display->drawString(x, y + 40, timeRemainingBuffer);

    int16_t compassX = 0, compassY = 0;
    uint16_t compassDiam = graphics::Screen::getCompassDiam(display->getWidth(), display->getHeight());

    // coordinates for the center of the compass/circle
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + display->getHeight() / 2;
    } else {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + FONT_HEIGHT_SMALL + (display->getHeight() - FONT_HEIGHT_SMALL) / 2;
    }
    display->drawCircle(compassX, compassY, compassDiam / 2);
    screen->drawCompassNorth(display, compassX, compassY, screen->getHeading() * PI / 180);
}

void MotionSensor::drawFrameGyroWarning(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // int x_offset = display->width() / 2;
    // int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(x, y, "Place Screen Face Up\n& Keep Still");

    uint8_t timeRemaining = (screen->getEndCalibration() - millis()) / 1000;
    sprintf(timeRemainingBuffer, "Starting in ( %02d )", timeRemaining);
    display->drawString(x, y + 40, timeRemainingBuffer);
}

void MotionSensor::drawFrameGyroCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // int x_offset = display->width() / 2;
    // int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Calibrating\nGyroscope");

    uint8_t timeRemaining = (screen->getEndCalibration() - millis()) / 1000;
    sprintf(timeRemainingBuffer, "Keep Still ( %02d )", timeRemaining);
    display->setFont(FONT_SMALL);
    display->drawString(x, y + 40, timeRemainingBuffer);
}
#endif

#if !MESHTASTIC_EXCLUDE_POWER_FSM
void MotionSensor::wakeScreen()
{
    if (powerFSM.getState() == &stateDARK) {
        LOG_DEBUG("Motion wakeScreen detected");
        powerFSM.trigger(EVENT_INPUT);
    }
}

void MotionSensor::buttonPress()
{
    LOG_DEBUG("Motion buttonPress detected");
    powerFSM.trigger(EVENT_PRESS);
}

#else

void MotionSensor::wakeScreen() {}

void MotionSensor::buttonPress() {}

#endif

void MotionSensor::getMagCalibrationData(float x, float y, float z)
{
    if (!showingScreen) {
        powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
        showingScreen = true;
        screen->startAlert((FrameCallback)drawFrameCalibration);
    }

    if (firstCalibrationRead) {
        magCalibrationMinMax.min.x = x;
        magCalibrationMinMax.max.x = x;
        magCalibrationMinMax.min.y = y;
        magCalibrationMinMax.max.y = y;
        magCalibrationMinMax.min.z = z;
        magCalibrationMinMax.max.z = z;
        firstCalibrationRead = false;
    } else {
        if (x > magCalibrationMinMax.max.x)
            magCalibrationMinMax.max.x = x;
        if (x < magCalibrationMinMax.min.x)
            magCalibrationMinMax.min.x = x;
        if (y > magCalibrationMinMax.max.y)
            magCalibrationMinMax.max.y = y;
        if (y < magCalibrationMinMax.min.y)
            magCalibrationMinMax.min.y = y;
        if (z > magCalibrationMinMax.max.z)
            magCalibrationMinMax.max.z = z;
        if (z < magCalibrationMinMax.min.z)
            magCalibrationMinMax.min.z = z;
    }

    uint32_t now = millis();
    if (now > endMagCalibrationAt) {
        sensorConfig.mAccel.x = (magCalibrationMinMax.max.x + magCalibrationMinMax.min.x) / 2;
        sensorConfig.mAccel.y = (magCalibrationMinMax.max.y + magCalibrationMinMax.min.y) / 2;
        sensorConfig.mAccel.z = (magCalibrationMinMax.max.z + magCalibrationMinMax.min.z) / 2;

        doMagCalibration = false;
        endMagCalibrationAt = 0;
        magCalibrationMinMax.min.x = 0;
        magCalibrationMinMax.max.x = 0;
        magCalibrationMinMax.min.y = 0;
        magCalibrationMinMax.max.y = 0;
        magCalibrationMinMax.min.z = 0;
        magCalibrationMinMax.max.z = 0;
        showingScreen = false;
        screen->endAlert();

        doGyroWarning = true;
        endGyroWarningAt = now + 10000;
        screen->setEndCalibration(endGyroWarningAt);
    }
}

void MotionSensor::gyroCalibrationWarning()
{
    if (!showingScreen) {
        powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
        showingScreen = true;
        screen->startAlert((FrameCallback)drawFrameGyroWarning);
    }

    uint32_t now = millis();
    if (now > endGyroWarningAt) {
        doGyroWarning = false;
        endGyroWarningAt = 0;
        showingScreen = false;
        screen->endAlert();

        doGyroCalibration = true;
        endGyroCalibrationAt = now + 10000;
        screen->setEndCalibration(endGyroCalibrationAt);
    }
}

void MotionSensor::getGyroCalibrationData(float g_x, float g_y, float g_z, float a_x, float a_y, float a_z)
{
    if (!showingScreen) {
        powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
        showingScreen = true;
        screen->startAlert((FrameCallback)drawFrameGyroCalibration);
    }

    gyroCalibrationSum.x += g_x;
    gyroCalibrationSum.y += g_y;
    gyroCalibrationSum.z += g_z;

    // increment x, y, or z based on greatest accel vector to identify down direction
    if (abs(a_x) > abs(a_y) && abs(a_x) > abs(a_z)) {
        if (a_x >= 0) {
            accelCalibrationSum.x += 1;
        } else {
            accelCalibrationSum.x += -1;
        }
    } else if (abs(a_y) > abs(a_x) && abs(a_y) > abs(a_z)) {
        if (a_y >= 0) {
            accelCalibrationSum.y += 1;
        } else {
            accelCalibrationSum.y += -1;
        }
    } else if (abs(a_z) > abs(a_x) && abs(a_z) > abs(a_y)) {
        if (a_z >= 0) {
            accelCalibrationSum.z += 1;
        } else {
            accelCalibrationSum.z += -1;
        }
    }
    calibrationCount++;

    LOG_DEBUG("Accel calibration x: %i, y: %i, z: %i", accelCalibrationSum.x, accelCalibrationSum.y, accelCalibrationSum.z);

    uint32_t now = millis();
    if (now > endGyroCalibrationAt) {
        sensorConfig.gyroAccel.x = gyroCalibrationSum.x / calibrationCount;
        sensorConfig.gyroAccel.y = gyroCalibrationSum.y / calibrationCount;
        sensorConfig.gyroAccel.z = gyroCalibrationSum.z / calibrationCount;

        // determine orientation multipliers based on down direction
        if (abs(accelCalibrationSum.x) > abs(accelCalibrationSum.y) && abs(accelCalibrationSum.x) > abs(accelCalibrationSum.z)) {
            if (accelCalibrationSum.x >= 0) {
                // X axis oriented with down positive
                sensorConfig.orientation.x = 1;
                sensorConfig.orientation.y = 1;
                sensorConfig.orientation.z = 1;
            } else {
                // X axis oriented with down negative
                sensorConfig.orientation.x = 1;
                sensorConfig.orientation.y = -1;
                sensorConfig.orientation.z = -1;
            }
        } else if (abs(accelCalibrationSum.y) > abs(accelCalibrationSum.x) &&
                   abs(accelCalibrationSum.y) > abs(accelCalibrationSum.z)) {
            if (accelCalibrationSum.y >= 0) {
                // Y axis oriented with down positive
                sensorConfig.orientation.x = 1;
                sensorConfig.orientation.y = 1;
                sensorConfig.orientation.z = 1;
            } else {
                // Y axis oriented with down negative
                sensorConfig.orientation.x = -1;
                sensorConfig.orientation.y = 1;
                sensorConfig.orientation.z = -1;
            }
        } else if (abs(accelCalibrationSum.z) > abs(accelCalibrationSum.x) &&
                   abs(accelCalibrationSum.z) > abs(accelCalibrationSum.y)) {
            if (accelCalibrationSum.z >= 0) {
                // Z axis oriented with down positive
                sensorConfig.orientation.x = 1;
                sensorConfig.orientation.y = 1;
                sensorConfig.orientation.z = 1;
            } else {
                // Z axis oriented with down negative
                sensorConfig.orientation.x = -1;
                sensorConfig.orientation.y = -1;
                sensorConfig.orientation.z = 1;
            }
        }

        LOG_INFO("Gyro center x: %.4f, y: %.4f, z: %.4f", sensorConfig.gyroAccel.x, sensorConfig.gyroAccel.y,
                 sensorConfig.gyroAccel.z);
        LOG_INFO("Orientation vector x: %i, y: %i, z: %i", sensorConfig.orientation.x, sensorConfig.orientation.y,
                 sensorConfig.orientation.z);

        saveState();
        doGyroCalibration = false;
        endGyroCalibrationAt = 0;
        accelCalibrationSum.x = 0;
        accelCalibrationSum.y = 0;
        accelCalibrationSum.z = 0;
        gyroCalibrationSum.x = 0;
        gyroCalibrationSum.y = 0;
        gyroCalibrationSum.z = 0;
        showingScreen = false;
        screen->endAlert();
    }
}

void MotionSensor::loadState()
{
#ifdef FSCom
    auto file = FSCom.open(configFileName, FILE_O_READ);
    if (file) {
        file.read((uint8_t *)&sensorState, MAX_STATE_BLOB_SIZE);
        file.close();

        memcpy(&sensorConfig, &sensorState, sizeof(SensorConfig));

        LOG_INFO("Motion Sensor config state read from %s", configFileName);
    } else {
        LOG_INFO("No Motion Sensor config state found (File: %s)", configFileName);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
}

void MotionSensor::saveState()
{
#ifdef FSCom
    memcpy(&sensorState, &sensorConfig, sizeof(SensorConfig));

    LOG_INFO("Save MAG calibration center_x: %.4f, center_Y: %.4f, center_Z: %.4f", sensorConfig.mAccel.x, sensorConfig.mAccel.y,
             sensorConfig.mAccel.z);
    LOG_INFO("Save GYRO calibration center_x: %.4f, center_Y: %.4f, center_Z: %.4f", sensorConfig.gyroAccel.x,
             sensorConfig.gyroAccel.y, sensorConfig.gyroAccel.z);
    LOG_INFO("Save ORIENT calibration: x=%i, y=%i, z=%i", sensorConfig.orientation.x, sensorConfig.orientation.y,
             sensorConfig.orientation.z);

    if (FSCom.exists(configFileName) && !FSCom.remove(configFileName)) {
        LOG_WARN("Can't remove old Motion Sensor config state file");
    }
    auto file = FSCom.open(configFileName, FILE_O_WRITE);
    if (file) {
        LOG_INFO("Write Motion Sensor config state to %s", configFileName);
        file.write((uint8_t *)&sensorState, MAX_STATE_BLOB_SIZE);
        file.flush();
        file.close();
    } else {
        LOG_INFO("Can't write Motion Sensor config state (File: %s)", configFileName);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
}

#endif