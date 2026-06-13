#include "MMC5983MASensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SparkFun_MMC5983MA_Arduino_Library.h>)

#include "detect/ScanI2CTwoWire.h"

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
extern graphics::Screen *screen;
#endif

static constexpr float MMC5983MA_ZERO_FIELD = 131072.0f;
static constexpr float MMC5983MA_COUNTS_PER_GAUSS = 16384.0f;
static constexpr uint16_t MMC5983MA_CONTINUOUS_FREQUENCY_HZ = 10;
static constexpr float MMC5983MA_HEADING_OFFSET_DEG = 180.0f;

MMC5983MASensor::MMC5983MASensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool MMC5983MASensor::init()
{
    LOG_DEBUG("MMC5983MA begin on addr 0x%02X (port=%d)", device.address.address, device.address.port);
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);

    if (!sensor.begin(*wire)) {
        LOG_DEBUG("MMC5983MA init error");
        return false;
    }

    sensor.softReset();
    sensor.setFilterBandwidth(100);
    sensor.performSetOperation();
    sensor.enableAutomaticSetReset();
    continuousMode = sensor.setContinuousModeFrequency(MMC5983MA_CONTINUOUS_FREQUENCY_HZ);
    continuousMode &= sensor.enableContinuousMode();

    if (!continuousMode) {
        LOG_DEBUG("MMC5983MA continuous mode failed, using single-shot reads");
        sensor.disableContinuousMode();
    }

    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    return true;
}

bool MMC5983MASensor::readMagnetometer(float &xGauss, float &yGauss, float &zGauss)
{
    uint32_t rawX = 0;
    uint32_t rawY = 0;
    uint32_t rawZ = 0;

    if (!(continuousMode ? sensor.readFieldsXYZ(&rawX, &rawY, &rawZ) : sensor.getMeasurementXYZ(&rawX, &rawY, &rawZ))) {
        LOG_DEBUG("MMC5983MA read failed");
        return false;
    }

    xGauss = ((float)rawX - MMC5983MA_ZERO_FIELD) / MMC5983MA_COUNTS_PER_GAUSS;
    yGauss = ((float)rawY - MMC5983MA_ZERO_FIELD) / MMC5983MA_COUNTS_PER_GAUSS;
    zGauss = ((float)rawZ - MMC5983MA_ZERO_FIELD) / MMC5983MA_COUNTS_PER_GAUSS;
    return true;
}

int32_t MMC5983MASensor::runOnce()
{
    float magX = 0, magY = 0, magZ = 0;
    if (!readMagnetometer(magX, magY, magZ)) {
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    if (doCalibration) {
        beginCalibrationDisplay(showingScreen);
        updateCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }
#endif

    magX -= (highestX + lowestX) / 2;
    magY -= (highestY + lowestY) / 2;
    magZ -= (highestZ + lowestZ) / 2;

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    float heading = atan2f(magY, magX) * RAD_TO_DEG + MMC5983MA_HEADING_OFFSET_DEG;
    if (heading < 0.0f) {
        heading += 360.0f;
    } else if (heading >= 360.0f) {
        heading -= 360.0f;
    }

    heading = applyCompassOrientation(heading);
    if (screen) {
        screen->setHeading(heading);
    }
#endif

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

void MMC5983MASensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    float xGauss = 0.0f;
    float yGauss = 0.0f;
    float zGauss = 0.0f;

    LOG_DEBUG("MMC5983MA calibration started for %is", forSeconds);
    if (readMagnetometer(xGauss, yGauss, zGauss)) {
        seedCalibrationExtrema(xGauss, yGauss, zGauss, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    } else {
        seedCalibrationExtrema(0.0f, 0.0f, 0.0f, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    }
    startCalibrationWindow(forSeconds);
#endif
}

#endif
