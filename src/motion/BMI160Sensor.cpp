#include "BMI160Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMI160)

#include "detect/ScanI2CTwoWire.h"

// Register map (Bosch BMI160 datasheet, BST-BMI160-DS000)
#define BMI160_REG_CHIP_ID 0x00      // Read-only, always 0xD1
#define BMI160_REG_ACC_X_L 0x12      // 0x12..0x17: accel X/Y/Z, 16-bit little-endian
#define BMI160_REG_ACC_CONF 0x40     // bits 7:4 bandwidth, bits 3:0 output data rate
#define BMI160_REG_ACC_RANGE 0x41    // bits 3:0 full-scale range
#define BMI160_REG_ACC_PWR_CTRL 0x7D // 0x04 enables the accelerometer
#define BMI160_REG_CMD 0x7E          // Command register

// The BMI160 has no PWR_CONF register; that one exists on the BMI270 only.
#define BMI160_CHIP_ID 0xD1

// Commands written to BMI160_REG_CMD
#define BMI160_CMD_SOFTRESET 0xB6
#define BMI160_CMD_ACC_NORMAL 0x11

// Accelerometer configuration
#define BMI160_ACC_BWP_NORMAL 0x20 // Bits 7:4, "normal" averaging of 4 samples
#define BMI160_ACC_ODR_100HZ 0x08  // Bits 3:0, 100 Hz
#define BMI160_ACC_RANGE_2G 0x03   // +-2 g
#define BMI160_ACC_PWR_ENABLE 0x04

// At +-2 g one g is 16384 LSB, so 3200 is roughly 0.2 g of change between
// samples. This matches the threshold the BMI270 driver uses.
#define BMI160_MOTION_THRESHOLD 3200

BMI160Sensor::BMI160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice)
{
    if (foundDevice.address.port == ScanI2C::I2CPort::WIRE1) {
        // Resolved via the scanner: WIRE1 may be a bridged bus rather than
        // the local Wire1 (e.g. SenseCAP Indicator)
        wire = ScanI2CTwoWire::fetchI2CBus(foundDevice.address);
    } else {
        wire = &Wire;
    }
}

bool BMI160Sensor::writeRegister(uint8_t reg, uint8_t value)
{
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->write(value);
    return wire->endTransmission() == 0;
}

uint8_t BMI160Sensor::readRegister(uint8_t reg)
{
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->endTransmission(false);
    wire->requestFrom(deviceAddress(), (uint8_t)1);
    if (wire->available()) {
        return wire->read();
    }
    return 0;
}

bool BMI160Sensor::readRegisters(uint8_t reg, uint8_t *data, size_t len)
{
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->endTransmission(false);
    size_t bytesRead = wire->requestFrom(deviceAddress(), (uint8_t)len);
    if (bytesRead != len) {
        // Drain whatever did arrive so the bus is left in a clean state, but
        // still report failure so the caller does not use a partial sample.
        for (size_t i = 0; i < bytesRead && wire->available(); i++) {
            data[i] = wire->read();
        }
        return false;
    }

    for (size_t i = 0; i < len && wire->available(); i++) {
        data[i] = wire->read();
    }
    return true;
}

bool BMI160Sensor::init()
{
    delay(10);

    // Confirm this really is a BMI160 before writing anything, in case another
    // device answers on the same address.
    uint8_t chipId = readRegister(BMI160_REG_CHIP_ID);
    if (chipId != BMI160_CHIP_ID) {
        LOG_WARN("BMI160: unexpected chip ID 0x%02X (expected 0xD1)", chipId);
        return false;
    }

    if (!writeRegister(BMI160_REG_CMD, BMI160_CMD_SOFTRESET)) {
        LOG_WARN("BMI160: soft reset write failed");
        return false;
    }
    delay(50);

    if (readRegister(BMI160_REG_CHIP_ID) != BMI160_CHIP_ID) {
        LOG_WARN("BMI160: chip ID lost after soft reset");
        return false;
    }

    if (!writeRegister(BMI160_REG_ACC_CONF, BMI160_ACC_BWP_NORMAL | BMI160_ACC_ODR_100HZ)) {
        LOG_WARN("BMI160: ACC_CONF write failed");
        return false;
    }
    if (!writeRegister(BMI160_REG_ACC_RANGE, BMI160_ACC_RANGE_2G)) {
        LOG_WARN("BMI160: ACC_RANGE write failed");
        return false;
    }
    if (!writeRegister(BMI160_REG_ACC_PWR_CTRL, BMI160_ACC_PWR_ENABLE)) {
        LOG_WARN("BMI160: ACC_PWR_CTRL write failed");
        return false;
    }
    delay(5);

    // Unlike the BMI270, which starts sampling as soon as PWR_CTRL is written,
    // the BMI160 needs an explicit normal-mode command in the command register.
    if (!writeRegister(BMI160_REG_CMD, BMI160_CMD_ACC_NORMAL)) {
        LOG_WARN("BMI160: normal mode command failed");
        return false;
    }
    // The mode change takes a few milliseconds to complete.
    delay(60);

    // Verify the data path actually works rather than trusting the write ACKs.
    uint8_t sample[6];
    if (!readRegisters(BMI160_REG_ACC_X_L, sample, sizeof(sample))) {
        LOG_WARN("BMI160: accelerometer read failed during init");
        return false;
    }

    initialized = true;
    return true;
}

int32_t BMI160Sensor::runOnce()
{
    if (!initialized) {
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    uint8_t data[6];
    if (!readRegisters(BMI160_REG_ACC_X_L, data, sizeof(data))) {
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    int16_t x = (int16_t)((data[1] << 8) | data[0]);
    int16_t y = (int16_t)((data[3] << 8) | data[2]);
    int16_t z = (int16_t)((data[5] << 8) | data[4]);

    if (!hasBaseline) {
        prevX = x;
        prevY = y;
        prevZ = z;
        hasBaseline = true;
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    // Change since the previous sample: near zero at rest, clearly non-zero
    // when the device is tapped or moved.
    //
    // Subtract in int32_t: the samples are int16_t, so an int16_t difference
    // can wrap when an axis swings across the full scale (about +/-1 g of
    // travel at 2 g). The wrapped result is small, so the strongest movement
    // would be the one case the threshold test misses.
    int32_t deltaX = abs((int32_t)x - (int32_t)prevX);
    int32_t deltaY = abs((int32_t)y - (int32_t)prevY);
    int32_t deltaZ = abs((int32_t)z - (int32_t)prevZ);

    // Track the baseline through a low-pass filter so a slow tilt does not
    // accumulate into a false motion event.
    prevX = (prevX * 9 + x) / 10;
    prevY = (prevY * 9 + y) / 10;
    prevZ = (prevZ * 9 + z) / 10;

    if (deltaX > BMI160_MOTION_THRESHOLD || deltaY > BMI160_MOTION_THRESHOLD ||
        deltaZ > BMI160_MOTION_THRESHOLD) {
        wakeScreen();
        return 500; // Sample more densely for a moment after motion
    }

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
