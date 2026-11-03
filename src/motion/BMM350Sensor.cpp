#include "BMM350Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && !MESHTASTIC_EXCLUDE_MAGNETOMETER && __has_include(<DFRobot_BMM350.h>)

#include "detect/ScanI2CTwoWire.h"
#include <cmath>

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
extern std::unique_ptr<graphics::Screen> screen;
#endif

static constexpr float BMM350_MIN_AXIS_RADIUS = 1.0f;
static constexpr int32_t BMM350_POLL_INTERVAL_MS = 200;

BMM350Sensor::BMM350Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor(foundDevice) {}

bool BMM350Sensor::init()
{
    if (!sensor)
        sensor = std::make_unique<DFRobot_BMM350_I2C>(ScanI2CTwoWire::fetchI2CBus(device.address), device.address.address);

    LOG_DEBUG("BMM350 begin on addr 0x%x (port=%d)", device.address.address, device.address.port);
    const uint8_t status = sensor->begin();
    if (status != 0) {
        LOG_WARN("BMM350 init error %u", status);
        return false;
    }

    sensor->setPresetMode(BMM350_PRESETMODE_ENHANCED, BMM350_DATA_RATE_12_5HZ);
    sensor->setMeasurementXYZ();
    sensor->setOperationMode(eBmm350NormalMode);
    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    return true;
}

bool BMM350Sensor::readMagnetometer(float &xMicroTesla, float &yMicroTesla, float &zMicroTesla)
{
    const sBmm350MagData_t data = sensor->getGeomagneticData();
    if (!std::isfinite(data.float_x) || !std::isfinite(data.float_y) || !std::isfinite(data.float_z) ||
        (data.float_x == 0.0f && data.float_y == 0.0f && data.float_z == 0.0f)) {
        return false;
    }

    xMicroTesla = data.float_x;
    yMicroTesla = data.float_y;
    zMicroTesla = data.float_z;
    return true;
}

int32_t BMM350Sensor::runOnce()
{
    float magX = 0, magY = 0, magZ = 0;
    if (!readMagnetometer(magX, magY, magZ)) {
        if (doCalibration)
            finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                       lowestZ);
        return BMM350_POLL_INTERVAL_MS;
    }

    if (doCalibration) {
        beginCalibrationDisplay(showingScreen);
        if (seedPending) {
            seedCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
            seedPending = false;
        }
        updateCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }

    magX -= (highestX + lowestX) * 0.5f;
    magY -= (highestY + lowestY) * 0.5f;
    magZ -= (highestZ + lowestZ) * 0.5f;

    const float radiusX = (highestX - lowestX) * 0.5f;
    const float radiusY = (highestY - lowestY) * 0.5f;
    const float radiusZ = (highestZ - lowestZ) * 0.5f;
    const float avgRadius = (radiusX + radiusY + radiusZ) / 3.0f;
    magX *= (radiusX > BMM350_MIN_AXIS_RADIUS) ? (avgRadius / radiusX) : 1.0f;
    magY *= (radiusY > BMM350_MIN_AXIS_RADIUS) ? (avgRadius / radiusY) : 1.0f;
    magZ *= (radiusZ > BMM350_MIN_AXIS_RADIUS) ? (avgRadius / radiusZ) : 1.0f;

    publishCompassMagSample(magX, magY, magZ);

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    float heading = atan2(double(magX), double(magY)) * RAD_TO_DEG;
    if (heading < 0.0f)
        heading += 360.0f;
    if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270)
        heading = 360.0f - heading;
    if (screen)
        screen->setHeading(applyCompassOrientation(heading));
#endif
    return BMM350_POLL_INTERVAL_MS;
}

void BMM350Sensor::calibrate(uint16_t forSeconds)
{
    LOG_DEBUG("BMM350 calibration started for %us", forSeconds);
    seedPending = true;
    startCalibrationWindow(forSeconds);
}

#endif