#include "MotionSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

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

#if !MESHTASTIC_EXCLUDE_POWER_FSM
void MotionSensor::wakeScreen()
{
    if (powerFSM.getState() == &stateDARK) {
        LOG_DEBUG("Accelerometer::wakeScreen detected\n");
        powerFSM.trigger(EVENT_INPUT);
    }
}

void MotionSensor::buttonPress()
{
    LOG_DEBUG("Accelerometer::buttonPress detected\n");
    powerFSM.trigger(EVENT_PRESS);
}
#else
void MotionSensor::wakeScreen()
{
    ;
}
void MotionSensor::buttonPress()
{
    ;
}
#endif

void MotionSensor::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    ;
}

#endif