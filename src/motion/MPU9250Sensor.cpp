#include "MPU9250Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#if !defined(MESHTASTIC_EXCLUDE_SCREEN)
// screen is defined in main.cpp
extern graphics::Screen *screen;
#endif

namespace
{
// MPU-6500 die registers
constexpr uint8_t MPU_SMPLRT_DIV = 0x19;
constexpr uint8_t MPU_CONFIG = 0x1A;
constexpr uint8_t MPU_GYRO_CONFIG = 0x1B;
constexpr uint8_t MPU_ACCEL_CONFIG = 0x1C;
constexpr uint8_t MPU_INT_PIN_CFG = 0x37;
constexpr uint8_t MPU_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t MPU_USER_CTRL = 0x6A;
constexpr uint8_t MPU_PWR_MGMT_1 = 0x6B;
constexpr uint8_t MPU_WHO_AM_I = 0x75;

// AK8963 magnetometer die registers
constexpr uint8_t AK_WIA = 0x00;
constexpr uint8_t AK_ST1 = 0x02;
constexpr uint8_t AK_HXL = 0x03;
constexpr uint8_t AK_ST2 = 0x09;
constexpr uint8_t AK_CNTL1 = 0x0A;
constexpr uint8_t AK_CNTL2 = 0x0B;
constexpr uint8_t AK_ASAX = 0x10;

// AK8963 control values
constexpr uint8_t AK_MODE_POWERDOWN = 0x00;
constexpr uint8_t AK_MODE_FUSE_ROM = 0x0F;
constexpr uint8_t AK_MODE_CONT2_16BIT = 0x16; // continuous mode 2 (100 Hz), 16-bit output

// Conversions
// ±2g full scale, 16-bit signed -> g per LSB
constexpr float ACCEL_LSB_PER_G = 16384.0f;
// AK8963 16-bit output: 0.15 µT per LSB (datasheet 6.3)
constexpr float MAG_UT_PER_LSB = 0.15f;
} // namespace

/**
 * @brief Construct the driver for a detected MPU-9250/MPU-9255 device.
 * @param foundDevice I2C scan result identifying the chip's address and bus port.
 */
MPU9250Sensor::MPU9250Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

/**
 * @brief Select the TwoWire bus instance the detected device lives on.
 * @return Pointer to Wire1 when the device was scanned on the second port, else Wire.
 */
TwoWire *MPU9250Sensor::resolveBus() const
{
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    return device.address.port == ScanI2C::I2CPort::WIRE1 ? &Wire1 : &Wire;
#else
    return &Wire;
#endif
}

/**
 * @brief Write a single byte to a register on the given I2C address.
 * @param i2cAddr Target die address (MPU-6500 or AK8963).
 * @param reg Register address to write.
 * @param value Byte value to store.
 * @return true if the I2C transaction completed successfully.
 */
bool MPU9250Sensor::writeRegister(uint8_t i2cAddr, uint8_t reg, uint8_t value)
{
    bus->beginTransmission(i2cAddr);
    bus->write(reg);
    bus->write(value);
    return bus->endTransmission() == 0;
}

/**
 * @brief Read a block of consecutive registers from an I2C address.
 * @param i2cAddr Target die address (MPU-6500 or AK8963).
 * @param reg First register address to read.
 * @param buf Destination buffer; must hold at least @p len bytes.
 * @param len Number of bytes to read.
 * @return true if exactly @p len bytes were received.
 */
bool MPU9250Sensor::readRegisters(uint8_t i2cAddr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    bus->beginTransmission(i2cAddr);
    bus->write(reg);
    if (bus->endTransmission(false) != 0) {
        return false;
    }
    const uint8_t received = bus->requestFrom((int)i2cAddr, (int)len);
    if (received != len) {
        return false;
    }
    for (uint8_t i = 0; i < len; ++i) {
        buf[i] = bus->read();
    }
    return true;
}

/**
 * @brief Reset and configure the MPU-6500 accelerometer/gyroscope die.
 *
 * Verifies WHO_AM_I, wakes the device onto the gyro PLL, sets the DLPF, sample
 * rate and full-scale ranges, then enables I2C bypass so the on-package AK8963
 * magnetometer becomes reachable on the main bus.
 * @return true only if every required register write succeeded.
 */
bool MPU9250Sensor::initMPU6500()
{
    const uint8_t addr = deviceAddress();

    // Sanity-check WHO_AM_I - scan should have already verified this but be defensive
    uint8_t whoAmI = 0;
    if (!readRegisters(addr, MPU_WHO_AM_I, &whoAmI, 1)) {
        LOG_DEBUG("MPU9250 WHO_AM_I read failed at 0x%02X", addr);
        return false;
    }
    if (whoAmI != 0x71 && whoAmI != 0x73) {
        LOG_DEBUG("MPU9250 unexpected WHO_AM_I 0x%02X at 0x%02X", whoAmI, addr);
        return false;
    }

    // Reset device
    if (!writeRegister(addr, MPU_PWR_MGMT_1, 0x80)) {
        return false;
    }
    delay(100);

    // Wake up, select PLL with X gyro reference (better stability than internal osc)
    if (!writeRegister(addr, MPU_PWR_MGMT_1, 0x01)) {
        return false;
    }
    delay(50);

    // DLPF 41 Hz (CONFIG = 3), sample rate 1 kHz / (1 + SMPLRT_DIV) = 200 Hz
    if (!writeRegister(addr, MPU_CONFIG, 0x03) || !writeRegister(addr, MPU_SMPLRT_DIV, 0x04)) {
        return false;
    }

    // ±2 g accel range
    if (!writeRegister(addr, MPU_ACCEL_CONFIG, 0x00)) {
        return false;
    }
    // ±250 dps gyro (gyro is unused for compass but configuring leaves the chip in a known state)
    if (!writeRegister(addr, MPU_GYRO_CONFIG, 0x00)) {
        return false;
    }

    // Expose AK8963 on the main I2C bus: disable internal master, enable bypass
    if (!writeRegister(addr, MPU_USER_CTRL, 0x00) || !writeRegister(addr, MPU_INT_PIN_CFG, 0x02)) {
        return false;
    }
    delay(10);

    return true;
}

/**
 * @brief Initialise the AK8963 magnetometer die.
 *
 * Confirms WHO_AM_I, reads the Fuse-ROM per-axis sensitivity adjustment (ASA)
 * into asaScale, then switches the magnetometer into 16-bit continuous mode 2.
 * @return true if the die responded and all setup writes succeeded.
 */
bool MPU9250Sensor::initAK8963()
{
    uint8_t wia = 0;
    if (!readRegisters(AK8963_ADDR, AK_WIA, &wia, 1) || wia != 0x48) {
        LOG_DEBUG("MPU9250 AK8963 WHO_AM_I unexpected (0x%02X)", wia);
        return false;
    }

    // Soft reset
    if (!writeRegister(AK8963_ADDR, AK_CNTL2, 0x01)) {
        return false;
    }
    delay(100);

    // Power-down then enter Fuse ROM access to read per-axis sensitivity (ASA)
    if (!writeRegister(AK8963_ADDR, AK_CNTL1, AK_MODE_POWERDOWN)) {
        return false;
    }
    delay(10);
    if (!writeRegister(AK8963_ADDR, AK_CNTL1, AK_MODE_FUSE_ROM)) {
        return false;
    }
    delay(10);

    uint8_t asa[3] = {0};
    if (!readRegisters(AK8963_ADDR, AK_ASAX, asa, 3)) {
        LOG_DEBUG("MPU9250 AK8963 ASA read failed");
        return false;
    }
    // Sensitivity adjustment: H_adj = H * ((ASA - 128) * 0.5 / 128 + 1) - datasheet 8.3.11
    for (int i = 0; i < 3; ++i) {
        asaScale[i] = (((float)asa[i] - 128.0f) * 0.5f / 128.0f) + 1.0f;
    }

    // Back to power-down, then enter continuous-mode-2 (100 Hz) at 16-bit resolution
    if (!writeRegister(AK8963_ADDR, AK_CNTL1, AK_MODE_POWERDOWN)) {
        return false;
    }
    delay(10);
    if (!writeRegister(AK8963_ADDR, AK_CNTL1, AK_MODE_CONT2_16BIT)) {
        return false;
    }
    delay(10);

    return true;
}

/**
 * @brief Bring the sensor fully online.
 *
 * Resolves the I2C bus, initialises both the MPU-6500 and AK8963 dies, and
 * loads any persisted hard-iron calibration.
 * @return true when the device is ready to produce headings.
 */
bool MPU9250Sensor::init()
{
    bus = resolveBus();
    if (!bus) {
        return false;
    }

    if (!initMPU6500()) {
        LOG_DEBUG("MPU9250 MPU-6500 init failed");
        return false;
    }
    if (!initAK8963()) {
        LOG_DEBUG("MPU9250 AK8963 init failed");
        return false;
    }

    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    LOG_DEBUG("MPU9250 init ok (asa=%.2f,%.2f,%.2f)", asaScale[0], asaScale[1], asaScale[2]);
    return true;
}

/**
 * @brief Read one accelerometer and magnetometer sample.
 * @param[out] accel Acceleration vector in g.
 * @param[out] mag Magnetic field vector in µT, sensitivity- and axis-scaled.
 * @return false if no fresh magnetometer sample was ready or the reading overflowed.
 */
bool MPU9250Sensor::readSensors(FusionVector &accel, FusionVector &mag)
{
    // Read 6 bytes of accel data
    uint8_t raw[6] = {0};
    if (!readRegisters(deviceAddress(), MPU_ACCEL_XOUT_H, raw, 6)) {
        return false;
    }
    const int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    const int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    const int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    accel.axis.x = (float)ax / ACCEL_LSB_PER_G;
    accel.axis.y = (float)ay / ACCEL_LSB_PER_G;
    accel.axis.z = (float)az / ACCEL_LSB_PER_G;

    // Check magnetometer data-ready bit
    uint8_t st1 = 0;
    if (!readRegisters(AK8963_ADDR, AK_ST1, &st1, 1) || (st1 & 0x01) == 0) {
        return false;
    }

    // Read 6 bytes of mag (little-endian) plus ST2 to release the data register.
    // ST2 also exposes the magnetic overflow flag (bit 3) - discard the sample if set.
    uint8_t magRaw[7] = {0};
    if (!readRegisters(AK8963_ADDR, AK_HXL, magRaw, 7)) {
        return false;
    }
    if (magRaw[6] & 0x08) {
        return false;
    }
    const int16_t mx = (int16_t)((magRaw[1] << 8) | magRaw[0]);
    const int16_t my = (int16_t)((magRaw[3] << 8) | magRaw[2]);
    const int16_t mz = (int16_t)((magRaw[5] << 8) | magRaw[4]);

    mag.axis.x = (float)mx * MAG_UT_PER_LSB * asaScale[0];
    mag.axis.y = (float)my * MAG_UT_PER_LSB * asaScale[1];
    mag.axis.z = (float)mz * MAG_UT_PER_LSB * asaScale[2];
    return true;
}

/**
 * @brief Periodic worker: sample, filter, compute tilt-compensated heading, publish.
 *
 * Applies hard-iron correction and a per-axis EMA, remaps the accel/mag frames
 * to a common orientation, computes the compass heading via the Fusion library,
 * and pushes it to the screen. Also drives the calibration state machine.
 * @return Milliseconds until the next desired invocation.
 */
int32_t MPU9250Sensor::runOnce()
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    FusionVector accel = {{0, 0, 0}};
    FusionVector mag = {{0, 0, 0}};
    if (!readSensors(accel, mag)) {
        // Missed samples must not stall calibration: close the window on timeout
        // even when no fresh magnetometer data is available.
        if (doCalibration) {
            finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                       lowestZ);
        }
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    if (doCalibration) {
        beginCalibrationDisplay(showingScreen);
        updateCalibrationExtrema(mag.axis.x, mag.axis.y, mag.axis.z, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }

    // Subtract hard-iron offsets
    mag.axis.x -= (highestX + lowestX) / 2;
    mag.axis.y -= (highestY + lowestY) / 2;
    mag.axis.z -= (highestZ + lowestZ) / 2;

    // Smooth raw inputs with a per-axis EMA to suppress dynamic acceleration
    // noise during device rotation.
    if (!filtersSeeded) {
        accelFiltered = accel;
        magFiltered = mag;
        filtersSeeded = true;
    } else {
        for (int i = 0; i < 3; ++i) {
            accelFiltered.array[i] = accelFilterAlpha * accel.array[i] + (1.0f - accelFilterAlpha) * accelFiltered.array[i];
            magFiltered.array[i] = magFilterAlpha * mag.array[i] + (1.0f - magFilterAlpha) * magFiltered.array[i];
        }
    }
    accel = accelFiltered;
    mag = magFiltered;

    // The MPU-6500 accelerometer and AK8963 magnetometer dies sit inside the
    // same package but use different axis conventions (datasheet 7.4 vs 8.1).
    // To express the mag reading in the accel frame we swap X<->Y and negate Z.
    // Final tweaks for board orientation are handled by config.display.compass_orientation.
    FusionVector ga, ma;
    ga.axis.x = accel.axis.x;
    ga.axis.y = -accel.axis.y;
    ga.axis.z = -accel.axis.z;
    ma.axis.x = mag.axis.y;
    ma.axis.y = mag.axis.x;
    ma.axis.z = -mag.axis.z;

    // Compensate for non-flat case mounting. FusionCompassCalculateHeading()
    // assumes Z is the up axis - when the baseboard is mounted vertically
    // (e.g. a case where the LCD sits perpendicular to the board), chip Z is
    // horizontal and the tilt-comp math becomes unstable. Override which chip
    // axis is treated as world-up via the MPU9250_UP_AXIS_* defines.
    //   _PZ (default) - chip +Z up (baseboard flat)
    //   _PX           - chip +X up (vertical mount, silkscreen "north" up)
    //   _NX           - chip -X up
    //   _PY           - chip +Y up
    //   _NY           - chip -Y up
#ifndef MPU9250_UP_AXIS
#define MPU9250_UP_AXIS MPU9250_UP_AXIS_PZ
#endif
#if MPU9250_UP_AXIS == MPU9250_UP_AXIS_PX
    ga = FusionAxesSwap(ga, FusionAxesAlignmentNZPYPX);
    ma = FusionAxesSwap(ma, FusionAxesAlignmentNZPYPX);
#elif MPU9250_UP_AXIS == MPU9250_UP_AXIS_PZ
    // Default orientation (chip +Z up), no remap needed.
#elif MPU9250_UP_AXIS == MPU9250_UP_AXIS_NX
    ga = FusionAxesSwap(ga, FusionAxesAlignmentPZPYNX);
    ma = FusionAxesSwap(ma, FusionAxesAlignmentPZPYNX);
#elif MPU9250_UP_AXIS == MPU9250_UP_AXIS_PY
    ga = FusionAxesSwap(ga, FusionAxesAlignmentPXNZPY);
    ma = FusionAxesSwap(ma, FusionAxesAlignmentPXNZPY);
#elif MPU9250_UP_AXIS == MPU9250_UP_AXIS_NY
    ga = FusionAxesSwap(ga, FusionAxesAlignmentPXPZNY);
    ma = FusionAxesSwap(ma, FusionAxesAlignmentPXPZNY);
#else
#error "MPU9250_UP_AXIS must be one of MPU9250_UP_AXIS_PZ/PX/NX/PY/NY"
#endif

    if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
        ma = FusionAxesSwap(ma, FusionAxesAlignmentNXNYPZ);
        ga = FusionAxesSwap(ga, FusionAxesAlignmentNXNYPZ);
    }

    float heading = FusionCompassCalculateHeading(FusionConventionNed, ga, ma);
    heading = applyCompassOrientation(heading);
    if (screen)
        screen->setHeading(heading);
#endif
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

/**
 * @brief Begin hard-iron calibration, seeding extrema from the current sample.
 * @param forSeconds Duration of the calibration window in seconds.
 */
void MPU9250Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    LOG_DEBUG("MPU9250 calibration started for %is", forSeconds);
    FusionVector accel, mag;
    if (readSensors(accel, mag)) {
        seedCalibrationExtrema(mag.axis.x, mag.axis.y, mag.axis.z, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    } else {
        seedCalibrationExtrema(0.0f, 0.0f, 0.0f, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    }
    startCalibrationWindow(forSeconds);
#endif
}

#endif
