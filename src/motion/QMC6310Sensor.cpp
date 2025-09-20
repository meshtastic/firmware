#include "QMC6310Sensor.h"
#include <Arduino.h>

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMC6310.hpp>)

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

QMC6310Sensor::QMC6310Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMC6310Sensor::init()
{
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    TwoWire &bus = (device.address.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
    TwoWire &bus = Wire; // fallback if only one I2C interface
#endif

    LOG_DEBUG("QMC6310: begin on addr 0x%02X (port=%d)", device.address.address, device.address.port);
    if (!sensor.begin(bus, device.address.address)) {
        LOG_DEBUG("QMC6310: init failed (begin)");
        return false;
    }

    uint8_t id = sensor.getChipID();
    LOG_DEBUG("QMC6310: chip id=0x%02x", id);

    // Configure magnetometer for continuous sampling
    int rc = sensor.configMagnetometer(SensorQMC6310::MODE_CONTINUOUS,  // mode
                                       SensorQMC6310::RANGE_2G,        // measurement range
                                       SensorQMC6310::DATARATE_50HZ,   // ODR
                                       SensorQMC6310::OSR_8,           // oversample
                                       SensorQMC6310::DSR_1);          // downsample
    if (rc < 0) {
        LOG_DEBUG("QMC6310: configMagnetometer failed (%d)", rc);
        return false;
    }

    // Optional: magnetic declination (degrees). Default 0.
    sensor.setDeclination(0.0f);

    LOG_DEBUG("QMC6310: init ok");
    return true;
}

int32_t QMC6310Sensor::runOnce()
{
    Polar p;
    if (sensor.readPolar(p)) {
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
        float heading = p.polar;
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
        if (screen)
            screen->setHeading(heading);
#endif
        // Throttled debug output (once per second)
        uint32_t now = millis();
        if (now - lastLogMs > 1000) {
            lastLogMs = now;
            LOG_DEBUG("QMC6310: heading=%.1f deg, |B|=%.1f uT", p.polar, p.uT);
        }
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
