#include "IIS2MDCTRSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#include "Fusion/Fusion.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "detect/ScanI2CTwoWire.h"
#include <math.h>

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
extern std::unique_ptr<graphics::Screen> screen;
#endif

namespace
{
constexpr uint8_t IIS2MDCTR_WHO_AM_I_REG = 0x4F;
constexpr uint8_t IIS2MDCTR_WHO_AM_I_VALUE = 0x40;
constexpr uint8_t IIS2MDCTR_CFG_REG_A = 0x60;
constexpr uint8_t IIS2MDCTR_CFG_REG_C = 0x62;
constexpr uint8_t IIS2MDCTR_STATUS_REG = 0x67;
constexpr uint8_t IIS2MDCTR_OUTX_L_REG = 0x68;

// offset for heading correction
#if defined(MAGNETOMETER_OFFSET)
static constexpr float IIS2MDCTR_HEADING_OFFSET_DEG = MAGNETOMETER_OFFSET;
#else
static constexpr float IIS2MDCTR_HEADING_OFFSET_DEG = 0.0f;
#endif

// COMP_TEMP_EN=1, ODR=100Hz ('11'), MD=continuous ('00')
constexpr uint8_t IIS2MDCTR_CFG_A_CONTINUOUS_100HZ = 0x8C;
// BDU=1 (block data update, avoids torn reads across the ODR boundary)
constexpr uint8_t IIS2MDCTR_CFG_C_BDU = 0x10;
constexpr uint8_t IIS2MDCTR_STATUS_ZYXDA = 0x08;

// 1.5 mGauss/LSB per the IIS2MDC datasheet.
constexpr float IIS2MDCTR_GAUSS_PER_LSB = 0.0015f;
} // namespace

static constexpr int32_t IIS2MDCTR_UPDATE_INTERVAL_MS = 20;
static constexpr uint32_t IIS2MDCTR_ACCEL_STALE_MS = 300;

IIS2MDCTRSensor::IIS2MDCTRSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool IIS2MDCTRSensor::writeRegister(uint8_t reg, uint8_t value)
{
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->write(value);
    return wire->endTransmission() == 0;
}

bool IIS2MDCTRSensor::readRegisters(uint8_t reg, uint8_t *buffer, size_t count)
{
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    if (wire->endTransmission(false) != 0) {
        return false;
    }
    if (wire->requestFrom(static_cast<uint8_t>(deviceAddress()), static_cast<uint8_t>(count)) != count) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        buffer[i] = wire->read();
    }
    return true;
}

bool IIS2MDCTRSensor::init()
{
    LOG_DEBUG("IIS2MDCTR begin on addr 0x%02X (port=%d)", deviceAddress(), devicePort());

    // Re-confirm identity before touching config registers
    uint8_t whoAmI = 0;
    if (!readRegisters(IIS2MDCTR_WHO_AM_I_REG, &whoAmI, 1) || whoAmI != IIS2MDCTR_WHO_AM_I_VALUE) {
        LOG_DEBUG("IIS2MDCTR WHO_AM_I mismatch (got 0x%02X)", whoAmI);
        return false;
    }

    if (!writeRegister(IIS2MDCTR_CFG_REG_A, IIS2MDCTR_CFG_A_CONTINUOUS_100HZ) ||
        !writeRegister(IIS2MDCTR_CFG_REG_C, IIS2MDCTR_CFG_C_BDU)) {
        LOG_DEBUG("IIS2MDCTR config failed");
        return false;
    }

    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    LOG_DEBUG("IIS2MDCTR init ok");
    return true;
}

bool IIS2MDCTRSensor::readMagnetometer(float &xGauss, float &yGauss, float &zGauss)
{
    uint8_t status = 0;
    if (!readRegisters(IIS2MDCTR_STATUS_REG, &status, 1) || !(status & IIS2MDCTR_STATUS_ZYXDA)) {
        return false;
    }

    uint8_t raw[6];
    if (!readRegisters(IIS2MDCTR_OUTX_L_REG, raw, sizeof(raw))) {
        return false;
    }

    const int16_t rawX = static_cast<int16_t>((raw[1] << 8) | raw[0]);
    const int16_t rawY = static_cast<int16_t>((raw[3] << 8) | raw[2]);
    const int16_t rawZ = static_cast<int16_t>((raw[5] << 8) | raw[4]);

    xGauss = rawX * IIS2MDCTR_GAUSS_PER_LSB;
    yGauss = rawY * IIS2MDCTR_GAUSS_PER_LSB;
    zGauss = rawZ * IIS2MDCTR_GAUSS_PER_LSB;

    return true;
}

int32_t IIS2MDCTRSensor::runOnce()
{

    static uint32_t lastAuxMagLogMs = 0;

    float magX = 0, magY = 0, magZ = 0;
    if (!readMagnetometer(magX, magY, magZ)) {
        return IIS2MDCTR_UPDATE_INTERVAL_MS;
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

    publishCompassMagSample(magX, magY, magZ);

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    float heading;
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;
    uint32_t accelAgeMs = 0;

    // Fuse with the latest accelerometer sample for tilt compensation.
    if (getLatestCompassAccelSample(accelX, accelY, accelZ, accelAgeMs) && accelAgeMs <= IIS2MDCTR_ACCEL_STALE_MS) {
        FusionVector ga = {.axis = {accelX, accelY, accelZ}};
        FusionVector ma = {.axis = {magX, magY, magZ}};
        heading = FusionCompass(ga, ma, FusionConventionNed) + IIS2MDCTR_HEADING_OFFSET_DEG;
        if (ga.axis.z > 0.0f)
            heading = 360.0f - heading;
    } else {
        heading = atan2(-double(magY), double(magX)) * RAD_TO_DEG + IIS2MDCTR_HEADING_OFFSET_DEG;
    }

    if (heading >= 360.0f)
        heading -= 360.0f;
    else if (heading < 0.0f)
        heading += 360.0f;

    heading = applyCompassOrientation(heading);
    if (screen)
        screen->setHeading(heading);
#endif

    return IIS2MDCTR_UPDATE_INTERVAL_MS;
}

void IIS2MDCTRSensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    float xGauss = 0.0f;
    float yGauss = 0.0f;
    float zGauss = 0.0f;

    LOG_DEBUG("IIS2MDCTR calibration started for %is", forSeconds);
    if (readMagnetometer(xGauss, yGauss, zGauss)) {
        seedCalibrationExtrema(xGauss, yGauss, zGauss, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    } else {
        seedCalibrationExtrema(0.0f, 0.0f, 0.0f, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    }
    startCalibrationWindow(forSeconds);
#endif
}

#endif
