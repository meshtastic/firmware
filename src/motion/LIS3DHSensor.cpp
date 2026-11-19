#include "LIS3DHSensor.h"
#include "NodeDB.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_LIS3DH.h>)

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "mesh/Throttle.h"
#endif

namespace
{
constexpr uint32_t DOUBLE_TAP_COOLDOWN_MS = 500;
}

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
#ifdef MESHTASTIC_INCLUDE_INKHUD
    if (stepTracker().isTracking()) {
        sensors_event_t event;
        if (sensor.getEvent(&event))
            sampleSteps(event.acceleration.x, event.acceleration.y, event.acceleration.z);
        if (lastButtonPress && !Throttle::hasElapsed(lastButtonPress, DOUBLE_TAP_COOLDOWN_MS))
            return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }
#endif
    if (const uint8_t click = sensor.getClick()) {
        if (!config.device.double_tap_as_button_press && config.display.wake_on_tap_or_motion) {
            wakeScreen();
        }

        if (config.device.double_tap_as_button_press && (click & 0x20)) {
#ifdef MESHTASTIC_INCLUDE_INKHUD
            lastButtonPress = Time::skipZero(Time::getMillis());
#endif
            buttonPress();
            return DOUBLE_TAP_COOLDOWN_MS;
        }
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
