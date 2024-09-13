#include "LIS3DHSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

LIS3DHSensor::LIS3DHSensor(ScanI2C::DeviceAddress address) : MotionSensor::MotionSensor(ScanI2C::DeviceType::LIS3DH, address) {}

bool LIS3DHSensor::init()
{
    if (sensor.begin(deviceAddress())) {
        sensor.setRange(LIS3DH_RANGE_2_G);
        // Adjust threshold, higher numbers are less sensitive
        sensor.setClick(config.device.double_tap_as_button_press ? 2 : 1, MOTION_SENSOR_CHECK_INTERVAL_MS);
        LOG_DEBUG("LIS3DHSensor::init ok\n");
        return true;
    }
    LOG_DEBUG("LIS3DHSensor::init failed\n");
    return false;
}

int32_t LIS3DHSensor::runOnce()
{
    if (sensor.getClick() > 0) {
        uint8_t click = sensor.getClick();
        if (!config.device.double_tap_as_button_press) {
            wakeScreen();
        }

        if (config.device.double_tap_as_button_press && (click & 0x20)) {
            buttonPress();
            return 500;
        }
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif