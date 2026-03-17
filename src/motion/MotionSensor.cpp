#include "MotionSensor.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "graphics/draw/CompassRenderer.h"
#include <cmath>

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

char timeRemainingBuffer[12];

namespace
{
constexpr uint32_t COMPASS_CALIBRATION_MAGIC = 0x4D43414CL; // "MCAL"
constexpr uint16_t COMPASS_CALIBRATION_VERSION = 1;

struct CompassCalibrationRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    float highestX;
    float lowestX;
    float highestY;
    float lowestY;
    float highestZ;
    float lowestZ;
};

bool isRangeValid(float highest, float lowest)
{
    return std::isfinite(highest) && std::isfinite(lowest) && (highest > lowest);
}
} // namespace

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

bool MotionSensor::saveMagnetometerCalibration(const char *filePath, float highestX, float lowestX, float highestY, float lowestY,
                                               float highestZ, float lowestZ)
{
#ifdef FSCom
    if (!isRangeValid(highestX, lowestX) || !isRangeValid(highestY, lowestY) || !isRangeValid(highestZ, lowestZ)) {
        LOG_WARN("Skipping save for invalid compass calibration ranges");
        return false;
    }

    FSCom.mkdir("/prefs");
    CompassCalibrationRecord record = {COMPASS_CALIBRATION_MAGIC, COMPASS_CALIBRATION_VERSION, 0, highestX, lowestX,
                                       highestY,                 lowestY,                    highestZ, lowestZ};

    auto file = SafeFile(filePath);
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    bool okay = (written == sizeof(record)) && file.close();
    if (okay) {
        LOG_INFO("Saved compass calibration to %s", filePath);
    } else {
        LOG_WARN("Failed to save compass calibration to %s", filePath);
    }
    return okay;
#else
    LOG_WARN("Cannot save compass calibration (filesystem unavailable)");
    return false;
#endif
}

bool MotionSensor::loadMagnetometerCalibration(const char *filePath, float &highestX, float &lowestX, float &highestY, float &lowestY,
                                               float &highestZ, float &lowestZ)
{
#ifdef FSCom
    CompassCalibrationRecord record = {};
    size_t bytesRead = 0;

    spiLock->lock();
    auto file = FSCom.open(filePath, FILE_O_READ);
    if (!file) {
        spiLock->unlock();
        LOG_INFO("No compass calibration file found at %s", filePath);
        return false;
    }
    bytesRead = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record));
    file.close();
    spiLock->unlock();

    if (bytesRead != sizeof(record)) {
        LOG_WARN("Compass calibration file has unexpected size: %s", filePath);
        return false;
    }
    if (record.magic != COMPASS_CALIBRATION_MAGIC || record.version != COMPASS_CALIBRATION_VERSION) {
        LOG_WARN("Compass calibration file has invalid header: %s", filePath);
        return false;
    }
    if (!isRangeValid(record.highestX, record.lowestX) || !isRangeValid(record.highestY, record.lowestY) ||
        !isRangeValid(record.highestZ, record.lowestZ)) {
        LOG_WARN("Compass calibration file contains invalid ranges: %s", filePath);
        return false;
    }

    highestX = record.highestX;
    lowestX = record.lowestX;
    highestY = record.highestY;
    lowestY = record.lowestY;
    highestZ = record.highestZ;
    lowestZ = record.lowestZ;

    LOG_INFO("Loaded compass calibration from %s", filePath);
    return true;
#else
    LOG_INFO("Filesystem unavailable, skipping compass calibration load");
    return false;
#endif
}

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
void MotionSensor::drawFrameCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (screen == nullptr)
        return;
    // int x_offset = display->width() / 2;
    // int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Calibrating\nCompass");

    const uint32_t now = millis();
    const uint32_t endCalibrationAt = screen->getEndCalibration();
    uint32_t timeRemaining = 0;
    if (endCalibrationAt > now) {
        timeRemaining = (endCalibrationAt - now + 999) / 1000;
    }
    snprintf(timeRemainingBuffer, sizeof(timeRemainingBuffer), "( %02lu )", (unsigned long)timeRemaining);
    display->setFont(FONT_SMALL);
    display->drawString(x, y + 40, timeRemainingBuffer);

    int16_t compassX = 0, compassY = 0;
    uint16_t compassDiam = graphics::CompassRenderer::getCompassDiam(display->getWidth(), display->getHeight());

    // coordinates for the center of the compass/circle
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + display->getHeight() / 2;
    } else {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + FONT_HEIGHT_SMALL + (display->getHeight() - FONT_HEIGHT_SMALL) / 2;
    }
    display->drawCircle(compassX, compassY, compassDiam / 2);
    graphics::CompassRenderer::drawCompassNorth(display, compassX, compassY, screen->getHeading() * PI / 180, (compassDiam / 2));
}
#endif

#if !MESHTASTIC_EXCLUDE_POWER_FSM
void MotionSensor::wakeScreen()
{
    if (powerFSM.getState() == &stateDARK) {
        LOG_DEBUG("Motion wakeScreen detected");
        if (config.display.wake_on_tap_or_motion)
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

#endif
