#include "ISM330DHCXSensor.h"
#include "NodeDB.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_ISM330DHCX.h>)

#include "Fusion/Fusion.h"
#include "Throttle.h"
#include "detect/ScanI2CTwoWire.h"
#include <math.h>

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
extern std::unique_ptr<graphics::Screen> screen;
#endif

// magnetometer_found is populated by the ScanI2C pass in main.cpp before this sensor's init()
// runs. A valid address here means a standalone magnetometer (e.g. IIS2MDCTR, etc.) already answers on
// the main I2C bus and is driven by its own MotionSensor - this driver must not also claim the
// IMU's Sensor Hub for it.
extern ScanI2C::DeviceAddress magnetometer_found;

namespace
{
// ISM330DHCX FUNC_CFG_ACCESS register banks (reg_access field, bits [7:6]).
constexpr uint8_t ISM330DHCX_FUNC_CFG_ACCESS = 0x01;
constexpr uint8_t ISM330DHCX_BANK_USER = 0x00;
constexpr uint8_t ISM330DHCX_BANK_SENSOR_HUB = 0x40;

// Sensor Hub bank registers (only valid while FUNC_CFG_ACCESS selects the Sensor Hub bank).
constexpr uint8_t ISM330DHCX_SENSOR_HUB_1 = 0x02;
constexpr uint8_t ISM330DHCX_STATUS_MASTER = 0x22;
constexpr uint8_t ISM330DHCX_MASTER_CONFIG = 0x14;
constexpr uint8_t ISM330DHCX_SLV0_ADD = 0x15;
constexpr uint8_t ISM330DHCX_SLV0_SUBADD = 0x16;
constexpr uint8_t ISM330DHCX_SLV0_CONFIG = 0x17;
constexpr uint8_t ISM330DHCX_DATAWRITE_SLV0 = 0x21;

// User-bank mirror of STATUS_MASTER; avoids a bank switch just to poll write completion.
constexpr uint8_t ISM330DHCX_STATUS_MASTER_MAINPAGE = 0x39;
constexpr uint8_t ISM330DHCX_STATUS_MASTER_WR_ONCE_DONE = 0x80;
constexpr uint8_t ISM330DHCX_STATUS_MASTER_SLAVE0_NACK = 0x08;

// MASTER_CONFIG bit positions.
constexpr uint8_t ISM330DHCX_MASTER_ON = 0x04;
constexpr uint8_t ISM330DHCX_SHUB_PU_EN = 0x08;
constexpr uint8_t ISM330DHCX_WRITE_ONCE = 0x40;
constexpr uint8_t ISM330DHCX_RST_MASTER_REGS = 0x80;
constexpr uint8_t ISM330DHCX_STATUS_MASTER_ENDOP = 0x01;

// Sensor hub write timeout
constexpr uint32_t ISM330DHCX_SHUB_WRITE_TIMEOUT_MS = 200;

// accelerometer to compass rotation offset
#if defined(ACCELEROMETER_OFFSET)
static constexpr float ISM330DHCX_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE = ACCELEROMETER_OFFSET;
#else
static constexpr float ISM330DHCX_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE = 0.0f;
#endif

// The IIS2MDCTR register map/constants this mirrors those in IIS2MDCTRSensor.cpp; duplicated
// here because the aux path talks to the magnetometer only through the IMU's Sensor Hub bank,
// never directly over Wire.
constexpr uint8_t IIS2MDCTR_I2C_ADDR = 0x1E;
constexpr uint8_t IIS2MDCTR_WHO_AM_I_REG = 0x4F;
constexpr uint8_t IIS2MDCTR_WHO_AM_I_VALUE = 0x40;
constexpr uint8_t IIS2MDCTR_CFG_REG_A = 0x60;
constexpr uint8_t IIS2MDCTR_CFG_REG_C = 0x62;
constexpr uint8_t IIS2MDCTR_OUTX_L_REG = 0x68;
constexpr uint8_t IIS2MDCTR_CFG_A_CONTINUOUS_100HZ = 0x8C;
constexpr uint8_t IIS2MDCTR_CFG_C_BDU = 0x10;
constexpr float IIS2MDCTR_GAUSS_PER_LSB = 0.0015f;

#if defined(MAGNETOMETER_OFFSET)
static constexpr float IIS2MDCTR_HEADING_OFFSET_DEG = MAGNETOMETER_OFFSET;
#else
static constexpr float IIS2MDCTR_HEADING_OFFSET_DEG = 270.0f;
#endif
} // namespace

ISM330DHCXSensor::ISM330DHCXSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool ISM330DHCXSensor::writeRegister(uint8_t reg, uint8_t value)
{
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->write(value);
    return wire->endTransmission() == 0;
}

bool ISM330DHCXSensor::readRegisters(uint8_t reg, uint8_t *buffer, size_t count)
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

bool ISM330DHCXSensor::setMemBank(uint8_t bank)
{
    return writeRegister(ISM330DHCX_FUNC_CFG_ACCESS, bank);
}

// Configures the ISM330DHCX's embedded Sensor Hub (I2C master) to detect and continuously read an
// IIS2MDCTR wired to the IMU's own aux SDx/SCx pins. Only called when no standalone magnetometer
// was found on the main bus. a "write-once" slave transaction configures the target chip's registers
// then SLV0 is reconfigured for a continuous read, triggered off the accel/gyro data-ready event.
bool ISM330DHCXSensor::configureAuxMagnetometer()
{
    // Probe: point SLV0 at the magnetometer's WHO_AM_I register and let the hub read it once.
    if (!setMemBank(ISM330DHCX_BANK_SENSOR_HUB)) {
        return false;
    }
    writeRegister(ISM330DHCX_SLV0_ADD, static_cast<uint8_t>((IIS2MDCTR_I2C_ADDR << 1) | 0x01)); // read
    writeRegister(ISM330DHCX_SLV0_SUBADD, IIS2MDCTR_WHO_AM_I_REG);
    writeRegister(ISM330DHCX_SLV0_CONFIG, 1); // numop=1
    writeRegister(ISM330DHCX_MASTER_CONFIG, ISM330DHCX_MASTER_ON | ISM330DHCX_SHUB_PU_EN);
    setMemBank(ISM330DHCX_BANK_USER);

    // The hub runs off the accel data-ready event; give it a couple of ODR periods, then check
    // for a NACK (no device answered on the aux bus) before trusting the read-back byte.
    delay(50);
    uint8_t status = 0;
    uint8_t whoAmI = 0;
    if (!readRegisters(ISM330DHCX_STATUS_MASTER_MAINPAGE, &status, 1) || (status & ISM330DHCX_STATUS_MASTER_SLAVE0_NACK)) {
        setMemBank(ISM330DHCX_BANK_SENSOR_HUB);
        writeRegister(ISM330DHCX_MASTER_CONFIG, 0); // no aux device present, stop the master
        setMemBank(ISM330DHCX_BANK_USER);
        return false;
    }
    if (!setMemBank(ISM330DHCX_BANK_SENSOR_HUB) || !readRegisters(ISM330DHCX_SENSOR_HUB_1, &whoAmI, 1) ||
        whoAmI != IIS2MDCTR_WHO_AM_I_VALUE) {
        setMemBank(ISM330DHCX_BANK_USER);
        return false;
    }

    // Push the magnetometer into continuous mode via two write-once slave transactions.
    const uint8_t cfg[2][2] = {{IIS2MDCTR_CFG_REG_A, IIS2MDCTR_CFG_A_CONTINUOUS_100HZ},
                               {IIS2MDCTR_CFG_REG_C, IIS2MDCTR_CFG_C_BDU}};
    for (const auto &pair : cfg) {
        writeRegister(ISM330DHCX_SLV0_ADD, static_cast<uint8_t>(IIS2MDCTR_I2C_ADDR << 1)); // write
        writeRegister(ISM330DHCX_SLV0_SUBADD, pair[0]);
        writeRegister(ISM330DHCX_DATAWRITE_SLV0, pair[1]);
        writeRegister(ISM330DHCX_MASTER_CONFIG, ISM330DHCX_MASTER_ON | ISM330DHCX_SHUB_PU_EN | ISM330DHCX_WRITE_ONCE);
        setMemBank(ISM330DHCX_BANK_USER);

        const uint32_t startMs = millis();
        bool done = false;
        while (millis() - startMs < ISM330DHCX_SHUB_WRITE_TIMEOUT_MS) {
            if (readRegisters(ISM330DHCX_STATUS_MASTER_MAINPAGE, &status, 1) &&
                (status & ISM330DHCX_STATUS_MASTER_WR_ONCE_DONE)) {
                done = true;
                break;
            }
            delay(5);
        }
        setMemBank(ISM330DHCX_BANK_SENSOR_HUB);
        if (!done || (status & ISM330DHCX_STATUS_MASTER_SLAVE0_NACK)) {
            writeRegister(ISM330DHCX_MASTER_CONFIG, 0);
            setMemBank(ISM330DHCX_BANK_USER);
            return false;
        }
    }

    // Reconfigure SLV0 for a continuous 6-byte read of OUTX_L_REG..OUTZ_H_REG, triggered by the
    // accel/gyro data-ready event (start_config=0), no longer write-once.
    // The hub only reloads SLV0_ADD/SLV0_SUBADD/SLV0_CONFIG on a MASTER_ON 0->1 transition, so it
    // must be stopped here first - otherwise it keeps relaying the stale 1-byte WHO_AM_I probe
    // read from earlier in this function forever, never picking up the new 6-byte OUTX_L config.
    // Pulse RST_MASTER_REGS to clear the leftover write-once state before the continuous read.
    writeRegister(ISM330DHCX_MASTER_CONFIG, ISM330DHCX_RST_MASTER_REGS);
    writeRegister(ISM330DHCX_MASTER_CONFIG, 0);
    writeRegister(ISM330DHCX_SLV0_ADD, static_cast<uint8_t>((IIS2MDCTR_I2C_ADDR << 1) | 0x01));
    writeRegister(ISM330DHCX_SLV0_SUBADD, IIS2MDCTR_OUTX_L_REG);
    writeRegister(ISM330DHCX_SLV0_CONFIG, 6); // numop=6
    writeRegister(ISM330DHCX_MASTER_CONFIG, ISM330DHCX_MASTER_ON | ISM330DHCX_SHUB_PU_EN);
    setMemBank(ISM330DHCX_BANK_USER);

    delay(50);
    status = 0;
    readRegisters(ISM330DHCX_STATUS_MASTER_MAINPAGE, &status, 1);
    LOG_DEBUG("ISM330DHCX hub status after continuous config: 0x%02x", status);
    if (!(status & ISM330DHCX_STATUS_MASTER_ENDOP)) {
        return false;
    }
    return true;
}

bool ISM330DHCXSensor::readAuxMagnetometer(float &xGauss, float &yGauss, float &zGauss)
{
    uint8_t raw[6];
    if (!setMemBank(ISM330DHCX_BANK_SENSOR_HUB) || !readRegisters(ISM330DHCX_SENSOR_HUB_1, raw, sizeof(raw))) {
        setMemBank(ISM330DHCX_BANK_USER);
        return false;
    }
    setMemBank(ISM330DHCX_BANK_USER);

    const int16_t rawX = static_cast<int16_t>((raw[1] << 8) | raw[0]);
    const int16_t rawY = static_cast<int16_t>((raw[3] << 8) | raw[2]);
    const int16_t rawZ = static_cast<int16_t>((raw[5] << 8) | raw[4]);
    xGauss = rawX * IIS2MDCTR_GAUSS_PER_LSB;
    yGauss = rawY * IIS2MDCTR_GAUSS_PER_LSB;
    zGauss = rawZ * IIS2MDCTR_GAUSS_PER_LSB;
    return true;
}

bool ISM330DHCXSensor::init()
{
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);
    if (sensor.begin_I2C(deviceAddress(), wire)) {
        // Default threshold of 2G, less sensitive options are 4, 8 or 16G
        sensor.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);

        // Duration is number of occurrences needed to trigger, higher threshold is less sensitive
        sensor.enableWakeup(config.display.wake_on_tap_or_motion, 1, ISM330DHCX_WAKE_THRESH);

        // Only take over the Sensor Hub if no standalone magnetometer already answers on the
        // main bus (that case is driven independently by IIS2MDCTRSensor).
        if (magnetometer_found.address == 0) {
            auxMagAvailable = configureAuxMagnetometer();
            LOG_DEBUG("ISM330DHCX aux magnetometer %s", auxMagAvailable ? "found" : "not found");
            if (auxMagAvailable) {
                loadMagnetometerCalibration(auxCompassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                            lowestZ);
            }
        }

        LOG_DEBUG("ISM330DHCX init ok");
        return true;
    }
    LOG_DEBUG("ISM330DHCX init failed");
    return false;
}

int32_t ISM330DHCXSensor::runOnce()
{
    static uint32_t lastAuxMagLogMs = 0;

    if (sensor.shake()) {
        wakeScreen();
        return 500;
    }

    sensors_event_t accel, gyro, temp;
    float ax, ay, az;
    bool haveAccel = sensor.getEvent(&accel, &gyro, &temp);
    ax = accel.acceleration.x;
    ay = accel.acceleration.y;
    az = accel.acceleration.z;

    if (haveAccel) {
        if (ISM330DHCX_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE != 0.0f) {
            static const float rotRad = ISM330DHCX_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE * DEG_TO_RAD;
            static const float cosTheta = cosf(rotRad);
            static const float sinTheta = sinf(rotRad);
            const float rotatedX = (ax * cosTheta) - (ay * sinTheta);
            const float rotatedY = (ax * sinTheta) + (ay * cosTheta);
            ax = rotatedX;
            ay = rotatedY;
        }
        publishCompassAccelSample(ax, ay, az);
    }

    if (auxMagAvailable) {
        float magX, magY, magZ;
        if (readAuxMagnetometer(magX, magY, magZ)) {
            publishCompassMagSample(magX, magY, magZ);

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
            if (doCalibration) {
                beginCalibrationDisplay(showingScreen);
                updateCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
                finishCalibrationIfExpired(showingScreen, auxCompassCalibrationFileName, highestX, lowestX, highestY, lowestY,
                                           highestZ, lowestZ);
            }
#endif

            // Hard-iron bias removal, same as the standalone IIS2MDCTR driver.
            magX -= (highestX + lowestX) * 0.5f;
            magY -= (highestY + lowestY) * 0.5f;
            magZ -= (highestZ + lowestZ) * 0.5f;

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
            if (haveAccel) {
                FusionVector ga = {.axis = {ax, ay, az}};
                FusionVector ma = {.axis = {magX, magY, magZ}};
                float heading = FusionCompass(ga, ma, FusionConventionNed) + IIS2MDCTR_HEADING_OFFSET_DEG;
                if (ga.axis.z > 0.0f)
                    heading = 360.0f - heading;

                if (heading >= 360.0f)
                    heading -= 360.0f;
                else if (heading < 0.0f)
                    heading += 360.0f;

                heading = applyCompassOrientation(heading);
                if (screen)
                    screen->setHeading(heading);
            }
#endif
        }
    }

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

void ISM330DHCXSensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
    if (!auxMagAvailable)
        return;

    float xGauss = 0.0f;
    float yGauss = 0.0f;
    float zGauss = 0.0f;

    LOG_DEBUG("ISM330DHCX aux magnetometer calibration started for %is", forSeconds);
    if (readAuxMagnetometer(xGauss, yGauss, zGauss)) {
        seedCalibrationExtrema(xGauss, yGauss, zGauss, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    } else {
        seedCalibrationExtrema(0.0f, 0.0f, 0.0f, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    }
    startCalibrationWindow(forSeconds);
#endif
}

#endif
