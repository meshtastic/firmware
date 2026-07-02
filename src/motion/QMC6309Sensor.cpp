#include "QMC6309Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMC6309.hpp>)

#include "Fusion/Fusion.h"
#include "detect/ScanI2CTwoWire.h"
#include <math.h>

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
extern graphics::Screen *screen;
#endif

static constexpr int32_t QMC6309_UPDATE_INTERVAL_MS = 20;
// Heading offset/flip below is a starting point copied from the MMC5983MA path; it is orientation-specific
// and must be verified/tuned against known North on real M9 hardware (see plan).
static constexpr float QMC6309_HEADING_OFFSET_DEG = 180.0f;
static constexpr uint32_t QMC6309_ACCEL_STALE_MS = 300;
static constexpr float QMC6309_MIN_AXIS_RADIUS = 1e-4f;

QMC6309Sensor::QMC6309Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMC6309Sensor::init()
{
    LOG_DEBUG("QMC6309 begin on addr 0x%02X (port=%d)", device.address.address, device.address.port);
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);

    if (!sensor.begin(*wire, deviceAddress())) {
        LOG_DEBUG("QMC6309 init error");
        return false;
    }

    sensor.reset();

    // 8 Gauss full-scale easily covers Earth's ~0.5 G field; OSR_8 for low noise. Tunable.
    if (!sensor.configMagnetometer(OperationMode::CONTINUOUS_MEASUREMENT, MagFullScaleRange::FS_8G, 100.0f,
                                   MagOverSampleRatio::OSR_8)) {
        LOG_DEBUG("QMC6309 config failed");
        return false;
    }

    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    LOG_DEBUG("QMC6309 init ok");
    LOG_DEBUG("QMC6309 calibration extrema: X=(%.3f, %.3f), Y=(%.3f, %.3f), Z=(%.3f, %.3f)", lowestX, highestX, lowestY, highestY,
              lowestZ, highestZ);
    return true;
}

bool QMC6309Sensor::readMagnetometer(float &xGauss, float &yGauss, float &zGauss)
{
    MagnetometerData data;
    if (!sensor.readData(data)) {
        return false;
    }

    // magnetic_field is already scaled to Gauss by the driver.
    xGauss = data.magnetic_field.x;
    yGauss = data.magnetic_field.y;
    zGauss = data.magnetic_field.z;
    return true;
}

int32_t QMC6309Sensor::runOnce()
{
    float magX = 0, magY = 0, magZ = 0;
    if (!readMagnetometer(magX, magY, magZ)) {
        return QMC6309_UPDATE_INTERVAL_MS;
    }

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    if (doCalibration) {
        beginCalibrationDisplay(showingScreen);
        updateCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }
#endif

    // Hard-iron bias removal.
    magX -= (highestX + lowestX) * 0.5f;
    magY -= (highestY + lowestY) * 0.5f;
    magZ -= (highestZ + lowestZ) * 0.5f;
    // LOG_WARN("QMC6309 extrema=(%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)",
    //          lowestX, lowestY, lowestZ, highestX, highestY, highestZ);

    // Soft-iron diagonal scaling from calibration extrema.
    const float radiusX = (highestX - lowestX) * 0.5f;
    const float radiusY = (highestY - lowestY) * 0.5f;
    const float radiusZ = (highestZ - lowestZ) * 0.5f;
    const float avgRadius = (radiusX + radiusY + radiusZ) / 3.0f;
    // magX *= (radiusX > QMC6309_MIN_AXIS_RADIUS) ? (avgRadius / radiusX) : 1.0f;
    // magY *= (radiusY > QMC6309_MIN_AXIS_RADIUS) ? (avgRadius / radiusY) : 1.0f;
    // magZ *= (radiusZ > QMC6309_MIN_AXIS_RADIUS) ? (avgRadius / radiusZ) : 1.0f;

    // Publish the calibrated magnetometer values (hard/soft-iron applied) for the optional on-screen debug readout.
    publishCompassMagSample(magX, magY, magZ);

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    float heading;
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;
    uint32_t accelAgeMs = 0;

    // Fuse with the latest accelerometer sample (published by the QMI8658 driver) for tilt compensation.
    if (getLatestCompassAccelSample(accelX, accelY, accelZ, accelAgeMs) && accelAgeMs <= QMC6309_ACCEL_STALE_MS) {
        FusionVector ga = {.axis = {accelX, accelY, accelZ}};
        FusionVector ma = {.axis = {magX, magY, magZ}};
        // if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
        // ma = FusionAxesSwap(ma, FusionAxesAlignmentNXNYPZ);
        // ga = FusionAxesSwap(ga, FusionAxesAlignmentNXNYPZ);
        //}
        // LOG_WARN("QMC6309 accel age %ums, ga=(%.3f, %.3f, %.3f), ma=(%.3f, %.3f, %.3f)", accelAgeMs, ga.axis.x, ga.axis.y,
        //          ga.axis.z, ma.axis.x, ma.axis.y, ma.axis.z);
        heading = FusionCompassCalculateHeading(FusionConventionNed, ga, ma);
        if (ga.axis.z > 0.0f)
            heading = 360.0f - heading;

    } else {
        heading = atan2f(-magY, magX) * RAD_TO_DEG;
    }

    if (heading >= 360.0f)
        heading -= 360.0f;
    else if (heading < 0.0f)
        heading += 360.0f;

    heading = applyCompassOrientation(heading);
    if (screen)
        screen->setHeading(heading);
#endif

    return QMC6309_UPDATE_INTERVAL_MS;
}

void QMC6309Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    float xGauss = 0.0f;
    float yGauss = 0.0f;
    float zGauss = 0.0f;

    LOG_DEBUG("QMC6309 calibration started for %is", forSeconds);
    if (readMagnetometer(xGauss, yGauss, zGauss)) {
        seedCalibrationExtrema(xGauss, yGauss, zGauss, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    } else {
        seedCalibrationExtrema(0.0f, 0.0f, 0.0f, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    }
    startCalibrationWindow(forSeconds);
#endif
}

#endif
