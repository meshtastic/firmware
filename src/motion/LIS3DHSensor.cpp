#include "LIS3DHSensor.h"
#include "NodeDB.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_LIS3DH.h>)

LIS3DHSensor::LIS3DHSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool LIS3DHSensor::init()
{
    // The SC7A20 shares the register map but identifies as 0x11.
    const uint8_t whoAmI = (deviceType() == ScanI2C::DeviceType::SC7A20) ? 0x11 : 0x33;
    if (sensor.begin(deviceAddress(), whoAmI)) {
        sensor.setRange(LIS3DH_RANGE_2_G);
        // Adjust threshold, higher numbers are less sensitive
        sensor.setClick(config.device.double_tap_as_button_press ? 2 : 1, MOTION_SENSOR_CHECK_INTERVAL_MS);
        LOG_DEBUG("%s init ok", deviceType() == ScanI2C::DeviceType::SC7A20 ? "SC7A20" : "LIS3DH");
        return true;
    }
    LOG_DEBUG("%s init failed", deviceType() == ScanI2C::DeviceType::SC7A20 ? "SC7A20" : "LIS3DH");
    return false;
}

int32_t LIS3DHSensor::runOnce()
{
    if (sensor.getClick() > 0) {
        uint8_t click = sensor.getClick();
        if (!config.device.double_tap_as_button_press && config.display.wake_on_tap_or_motion) {
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
