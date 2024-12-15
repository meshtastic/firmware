#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

char timeRemainingBuffer[12];

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
        sensorConfig.mAccel.min.x = x;
        sensorConfig.mAccel.max.x = x;
        sensorConfig.mAccel.min.y = y;
        sensorConfig.mAccel.max.y = y;
        sensorConfig.mAccel.min.z = z;
        sensorConfig.mAccel.max.z = z;
        firstCalibrationRead = false;
    } else {
        if (x > sensorConfig.mAccel.max.x)
            sensorConfig.mAccel.max.x = x;
        if (x < sensorConfig.mAccel.min.x)
            sensorConfig.mAccel.min.x = x;
        if (y > sensorConfig.mAccel.max.y)
            sensorConfig.mAccel.max.y = y;
        if (y < sensorConfig.mAccel.min.y)
            sensorConfig.mAccel.min.y = y;
        if (z > sensorConfig.mAccel.max.z)
            sensorConfig.mAccel.max.z = z;
        if (z < sensorConfig.mAccel.min.z)
            sensorConfig.mAccel.min.z = z;
    }

    uint32_t now = millis();
    if (now > endCalibrationAt) {
        doCalibration = false;
        endCalibrationAt = 0;
        showingScreen = false;
        screen->endAlert();

        saveState();
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

    LOG_INFO("Motion Sensor save calibration min_x: %.4f, max_X: %.4f, min_Y: %.4f, max_Y: %.4f, min_Z: %.4f, max_Z: %.4f",
             sensorConfig.mAccel.min.x, sensorConfig.mAccel.max.x, sensorConfig.mAccel.min.y, sensorConfig.mAccel.max.y,
             sensorConfig.mAccel.min.z, sensorConfig.mAccel.max.z);

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