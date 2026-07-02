#include "MotionSensor.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "graphics/draw/CompassRenderer.h"

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
    // NaN/Inf guard without pulling in extra math helpers.
    return (highest == highest) && (lowest == lowest) && (highest > lowest);
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
        return false;
    }

    FSCom.mkdir("/prefs");
    CompassCalibrationRecord record = {
        COMPASS_CALIBRATION_MAGIC, COMPASS_CALIBRATION_VERSION, 0, highestX, lowestX, highestY, lowestY, highestZ, lowestZ};

    auto file = SafeFile(filePath, true);
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
    return (written == sizeof(record)) && file.close();
#else
    return false;
#endif
}

bool MotionSensor::loadMagnetometerCalibration(const char *filePath, float &highestX, float &lowestX, float &highestY,
                                               float &lowestY, float &highestZ, float &lowestZ)
{
#ifdef FSCom
    CompassCalibrationRecord record = {};
    size_t bytesRead = 0;

    spiLock->lock();
    auto file = FSCom.open(filePath, FILE_O_READ);
    if (!file) {
        spiLock->unlock();
        return false;
    }
    bytesRead = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record));
    file.close();
    spiLock->unlock();

    const bool headerValid = (bytesRead == sizeof(record)) && (record.magic == COMPASS_CALIBRATION_MAGIC) &&
                             (record.version == COMPASS_CALIBRATION_VERSION) && (record.reserved == 0U);
    const bool rangeValid = isRangeValid(record.highestX, record.lowestX) && isRangeValid(record.highestY, record.lowestY) &&
                            isRangeValid(record.highestZ, record.lowestZ);
    if (!headerValid || !rangeValid) {
        return false;
    }

    highestX = record.highestX;
    lowestX = record.lowestX;
    highestY = record.highestY;
    lowestY = record.lowestY;
    highestZ = record.highestZ;
    lowestZ = record.lowestZ;

    return true;
#else
    return false;
#endif
}

void MotionSensor::beginCalibrationDisplay(bool &showingScreen)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    if (!showingScreen) {
        powerFSM.trigger(EVENT_PRESS); // keep screen alive during calibration
        showingScreen = true;
        if (screen)
            screen->startAlert((FrameCallback)drawFrameCalibration);
    }
#else
    (void)showingScreen;
#endif
}

void MotionSensor::finishCalibrationIfExpired(bool &showingScreen, const char *filePath, float highestX, float lowestX,
                                              float highestY, float lowestY, float highestZ, float lowestZ)
{
    const uint32_t now = millis();
    if ((int32_t)(now - endCalibrationAt) < 0)
        return;

    doCalibration = false;
    endCalibrationAt = 0;
    showingScreen = false;
    saveMagnetometerCalibration(filePath, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    if (screen) {
        screen->setEndCalibration(0);
        screen->endAlert();
    }
#endif
}

void MotionSensor::startCalibrationWindow(uint16_t forSeconds)
{
    doCalibration = true;
    const uint32_t calibrateFor = static_cast<uint32_t>(forSeconds) * 1000U;
    endCalibrationAt = millis() + calibrateFor;
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    if (screen)
        screen->setEndCalibration(endCalibrationAt);
#endif
}

void MotionSensor::seedCalibrationExtrema(float x, float y, float z, float &highestX, float &lowestX, float &highestY,
                                          float &lowestY, float &highestZ, float &lowestZ)
{
    highestX = lowestX = x;
    highestY = lowestY = y;
    highestZ = lowestZ = z;
}

void MotionSensor::updateCalibrationExtrema(float x, float y, float z, float &highestX, float &lowestX, float &highestY,
                                            float &lowestY, float &highestZ, float &lowestZ)
{
    if (x > highestX)
        highestX = x;
    if (x < lowestX)
        lowestX = x;
    if (y > highestY)
        highestY = y;
    if (y < lowestY)
        lowestY = y;
    if (z > highestZ)
        highestZ = z;
    if (z < lowestZ)
        lowestZ = z;
}

float MotionSensor::applyCompassOrientation(float heading)
{
    switch (config.display.compass_orientation) {
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90_INVERTED:
        return heading + 90;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180_INVERTED:
        return heading + 180;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270_INVERTED:
        return heading + 270;
    default:
        return heading;
    }
}

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
void MotionSensor::drawFrameCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (screen == nullptr)
        return;

    const int16_t width = display->getWidth();
    const int16_t height = display->getHeight();
    const bool compactLayout = (height <= 80);
    const int16_t margin = 4;

    const uint32_t now = millis();
    const uint32_t endCalibrationAt = screen->getEndCalibration();
    uint32_t timeRemaining = 0;
    if (endCalibrationAt > now) {
        timeRemaining = (endCalibrationAt - now + 999) / 1000;
    }

    int16_t compassX = 0, compassY = 0;
    uint16_t compassDiam = graphics::CompassRenderer::getCompassDiam(width, height);
    const int16_t compassRadius = compassDiam / 2;

    // coordinates for the center of the compass/circle
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + width - compassRadius - margin;
        compassY = y + height / 2;
    } else {
        compassX = x + width - compassRadius - margin;
        compassY = y + FONT_HEIGHT_SMALL + (height - FONT_HEIGHT_SMALL) / 2;
    }

    const int16_t textLeft = x + 1;
    const int16_t textRight = compassX - compassRadius - margin;
    const int16_t textWidth = textRight - textLeft;
    int16_t lineY = y;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    if (textWidth > 12) {
        const char *title = "Cal";
        const char *line1 = "Figure-8";
        const char *line2 = "Rotate axes";
        const char *line3 = "Away from metal";

        display->setFont(FONT_SMALL);
        if (!compactLayout && display->getStringWidth("Compass Calibration") <= textWidth) {
            display->setFont(FONT_MEDIUM);
            title = "Compass Calibration";
            line1 = "Move in figure-8";
            line2 = "Rotate all axes";
            line3 = "Keep from metal";
            display->drawString(textLeft, lineY, title);
            lineY += FONT_HEIGHT_MEDIUM;
            display->setFont(FONT_SMALL);
        } else if (display->getStringWidth("Compass Cal") <= textWidth) {
            title = "Compass Cal";
            if (textWidth >= display->getStringWidth("Move in figure-8")) {
                line1 = "Move in figure-8";
                line2 = "Rotate all axes";
                line3 = "Keep from metal";
            }
            display->drawString(textLeft, lineY, title);
            lineY += FONT_HEIGHT_SMALL;
        } else {
            display->drawString(textLeft, lineY, title);
            lineY += FONT_HEIGHT_SMALL;
        }

        display->drawString(textLeft, lineY, line1);
        lineY += FONT_HEIGHT_SMALL;
        display->drawString(textLeft, lineY, line2);
        lineY += FONT_HEIGHT_SMALL;
        if (!compactLayout || textWidth >= display->getStringWidth(line3)) {
            display->drawString(textLeft, lineY, line3);
        }
    }

    if (textWidth >= display->getStringWidth("000s left")) {
        snprintf(timeRemainingBuffer, sizeof(timeRemainingBuffer), "%lus left", (unsigned long)timeRemaining);
    } else {
        snprintf(timeRemainingBuffer, sizeof(timeRemainingBuffer), "%lus", (unsigned long)timeRemaining);
    }
    display->setFont(FONT_SMALL);
    if (textWidth > 12) {
        display->drawString(textLeft, y + height - FONT_HEIGHT_SMALL - 1, timeRemainingBuffer);
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
