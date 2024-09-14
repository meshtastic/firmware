#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

// screen is defined in main.cpp
extern graphics::Screen *screen;

MotionSensor::MotionSensor(ScanI2C::DeviceType device, ScanI2C::DeviceAddress address)
{
    _device.address.address = address.address;
    _device.address.port = address.port;
    _device.type = device;
    LOG_DEBUG("MotionSensor::MotionSensor port: %s address: 0x%x type: %d\n",
              devicePort() == ScanI2C::I2CPort::WIRE1 ? "Wire1" : "Wire", (uint8_t)deviceAddress(), deviceType());
}

ScanI2C::DeviceType MotionSensor::deviceType()
{
    return _device.type;
}

uint8_t MotionSensor::deviceAddress()
{
    return _device.address.address;
}

ScanI2C::I2CPort MotionSensor::devicePort()
{
    return _device.address.port;
}

void MotionSensor::drawFrameCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // int x_offset = display->width() / 2;
    // int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Calibrating\nCompass");
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

#if !MESHTASTIC_EXCLUDE_POWER_FSM
void MotionSensor::wakeScreen()
{
    if (powerFSM.getState() == &stateDARK) {
        LOG_DEBUG("MotionSensor::wakeScreen detected\n");
        powerFSM.trigger(EVENT_INPUT);
    }
}

void MotionSensor::buttonPress()
{
    LOG_DEBUG("MotionSensor::buttonPress detected\n");
    powerFSM.trigger(EVENT_PRESS);
}

#else

void MotionSensor::wakeScreen() {}

void MotionSensor::buttonPress() {}

#endif

#endif