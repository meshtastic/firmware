#include "QMC6310Sensor.h"
#include <Arduino.h>
#include "SensorLiveData.h"

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
    // Read and process raw values with simple hard‑iron calibration
    if (sensor.isDataReady()) {
        sensor.readData();
        int16_t rx = sensor.getRawX();
        int16_t ry = sensor.getRawY();
        int16_t rz = sensor.getRawZ();

        if (rx < minX)
            minX = rx;
        if (rx > maxX)
            maxX = rx;
        if (ry < minY)
            minY = ry;
        if (ry > maxY)
            maxY = ry;
        if (rz < minZ)
            minZ = rz;
        if (rz > maxZ)
            maxZ = rz;

        offsetX = (maxX + minX) * 0.5f;
        offsetY = (maxY + minY) * 0.5f;
        offsetZ = (maxZ + minZ) * 0.5f;

        float mx = float(rx) - offsetX;
        float my = float(ry) - offsetY;
        float mz = float(rz) - offsetZ;

        // Soft-iron scaling based on radii along axes
        float radiusX = (maxX - minX) * 0.5f;
        float radiusY = (maxY - minY) * 0.5f;
        float radiusZ = (maxZ - minZ) * 0.5f;
        float avgR = 0.0f; int rcount = 0;
        if (radiusX > 1) { avgR += radiusX; rcount++; }
        if (radiusY > 1) { avgR += radiusY; rcount++; }
        if (radiusZ > 1) { avgR += radiusZ; rcount++; }
        if (rcount > 0) avgR /= rcount; else avgR = 1.0f;
        scaleX = (radiusX > 1) ? (avgR / radiusX) : 1.0f;
        scaleY = (radiusY > 1) ? (avgR / radiusY) : 1.0f;
        scaleZ = (radiusZ > 1) ? (avgR / radiusZ) : 1.0f;

        mx *= scaleX; my *= scaleY; mz *= scaleZ;

        // Axis mapping / sign
        float hx = mx, hy = my;
#if QMC6310_SWAP_XY
        hx = my; hy = mx;
#endif
        hx *= (float)QMC6310_X_SIGN;
        hy *= (float)QMC6310_Y_SIGN;

        float heading;
#if QMC6310_HEADING_STYLE == 1
        heading = atan2f(hx, -hy) * 180.0f / PI; // QST library style
#else
        heading = atan2f(hy, hx) * 180.0f / PI;  // Arduino sketch style
#endif
        heading += QMC6310_DECLINATION_DEG + QMC6310_YAW_MOUNT_OFFSET;
        while (heading < 0.0f) heading += 360.0f;
        while (heading >= 360.0f) heading -= 360.0f;

        g_qmc6310Live.initialized = true;
        g_qmc6310Live.rawX = rx;
        g_qmc6310Live.rawY = ry;
        g_qmc6310Live.rawZ = rz;
        g_qmc6310Live.offX = offsetX;
        g_qmc6310Live.offY = offsetY;
        g_qmc6310Live.offZ = offsetZ;
        // Update live data (also publish scaled µT)
        const float GAUSS_PER_LSB = QMC6310_SENS_GAUSS_PER_LSB;
        g_qmc6310Live.uT_X = mx * GAUSS_PER_LSB * 100.0f;
        g_qmc6310Live.uT_Y = my * GAUSS_PER_LSB * 100.0f;
        g_qmc6310Live.uT_Z = mz * GAUSS_PER_LSB * 100.0f;
        g_qmc6310Live.scaleX = scaleX;
        g_qmc6310Live.scaleY = scaleY;
        g_qmc6310Live.scaleZ = scaleZ;
        g_qmc6310Live.heading = heading;
        g_qmc6310Live.last_ms = millis();

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
        switch (config.display.compass_orientation) {
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0_INVERTED:
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0:
            break;
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90:
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90_INVERTED:
            heading += 90;
            if (heading >= 360.0f) heading -= 360.0f;
            break;
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180:
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180_INVERTED:
            heading += 180;
            if (heading >= 360.0f) heading -= 360.0f;
            break;
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270:
        case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270_INVERTED:
            heading += 270;
            if (heading >= 360.0f) heading -= 360.0f;
            break;
        }
        if (screen)
            screen->setHeading(heading);
#endif

        uint32_t now = millis();
        if (now - lastLogMs > 1000) {
            lastLogMs = now;
            LOG_DEBUG("QMC6310: head=%.1f off[x=%.0f y=%.0f z=%.0f] raw[x=%d y=%d z=%d]",
                      heading, offsetX, offsetY, offsetZ, rx, ry, rz);
        }
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
