// Wire.cpp - Arduino TwoWire backed by Zephyr i2c30 (TWIM30 hardware).
//
// The pinctrl + clock-frequency are configured in
// zephyr/boards/nrf54l15dk_nrf54l15_cpuapp.overlay. Runtime begin()/setClock()
// are best-effort: setClock() goes through i2c_configure() to actually change
// the bus speed; begin() just verifies the device is ready.

#include "Wire.h"
#include "configuration.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

// Resolve the i2c30 node at compile time. If the overlay has not enabled
// i2c30, this evaluates to a NULL device pointer and every call short-circuits
// to a NACK return code - matching the prior compile-only stub behavior.
#define I2C_NODE DT_NODELABEL(i2c30)

static const struct device *getI2CDevice()
{
#if DT_NODE_HAS_STATUS(I2C_NODE, okay)
    static const struct device *const dev = DEVICE_DT_GET(I2C_NODE);
    return dev;
#else
    return nullptr;
#endif
}

// Wire/Wire1 instances are defined in nrf54l15_arduino.cpp alongside the
// other Arduino singletons (Serial, SPI, …).

TwoWire::TwoWire() : txAddr(0), txLen(0), txBuf{}, rxLen(0), rxPos(0), rxBuf{} {}

void TwoWire::begin()
{
    const struct device *dev = getI2CDevice();
    if (dev == nullptr) {
        LOG_WARN("Wire.begin(): i2c30 not enabled in DT overlay");
        return;
    }
    if (!device_is_ready(dev)) {
        LOG_WARN("Wire.begin(): i2c30 device not ready");
        return;
    }
    LOG_INFO("Wire.begin(): i2c30 ready");
}

void TwoWire::begin(uint8_t /*sda*/, uint8_t /*scl*/)
{
    // SDA/SCL fixed by overlay pinctrl - pin args ignored.
    begin();
}

void TwoWire::begin(int /*sda*/, int /*scl*/, uint32_t freq)
{
    begin();
    if (freq) {
        setClock(freq);
    }
}

void TwoWire::end()
{
    // No-op: Zephyr i2c devices stay initialized for the lifetime of the
    // application. Runtime PM (zephyr,pm-device-runtime-auto in the DT)
    // handles low-power transitions when idle.
}

void TwoWire::setClock(uint32_t freq)
{
    const struct device *dev = getI2CDevice();
    if (dev == nullptr) {
        return;
    }
    uint32_t speed;
    if (freq >= 1000000U) {
        speed = I2C_SPEED_FAST_PLUS; // 1 MHz
    } else if (freq >= 400000U) {
        speed = I2C_SPEED_FAST; // 400 kHz
    } else {
        speed = I2C_SPEED_STANDARD; // 100 kHz
    }
    uint32_t cfg = I2C_MODE_CONTROLLER | I2C_SPEED_SET(speed);
    int rc = i2c_configure(dev, cfg);
    if (rc) {
        LOG_WARN("Wire.setClock(%u) failed: %d", (unsigned)freq, rc);
    }
}

void TwoWire::beginTransmission(uint8_t addr)
{
    txAddr = addr;
    txLen = 0;
}

size_t TwoWire::write(uint8_t data)
{
    if (txLen >= WIRE_BUFFER_LENGTH) {
        return 0; // overflow - endTransmission() will return 1
    }
    txBuf[txLen++] = data;
    return 1;
}

size_t TwoWire::write(const uint8_t *data, size_t n)
{
    size_t written = 0;
    for (size_t i = 0; i < n; i++) {
        if (write(data[i]) == 0) {
            break;
        }
        written++;
    }
    return written;
}

uint8_t TwoWire::endTransmission(bool /*stop*/)
{
    // Arduino return codes:
    //   0 = success
    //   1 = data-too-long (overflow caught in write())
    //   2 = NACK on address
    //   3 = NACK on data
    //   4 = other error
    //   5 = timeout
    if (txLen > WIRE_BUFFER_LENGTH) {
        return 1;
    }
    const struct device *dev = getI2CDevice();
    if (dev == nullptr || !device_is_ready(dev)) {
        return 4;
    }
    int rc = i2c_write(dev, txBuf, txLen, txAddr);
    txLen = 0;
    if (rc == 0) {
        return 0;
    }
    if (rc == -EIO) {
        return 2; // address NACK is the most common -EIO source on nrf-twim
    }
    if (rc == -ETIMEDOUT) {
        return 5;
    }
    return 4;
}

uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t quantity, bool /*stop*/)
{
    rxLen = 0;
    rxPos = 0;
    if (quantity == 0) {
        return 0;
    }
    if (quantity > WIRE_BUFFER_LENGTH) {
        quantity = WIRE_BUFFER_LENGTH;
    }
    const struct device *dev = getI2CDevice();
    if (dev == nullptr || !device_is_ready(dev)) {
        return 0;
    }

    // If there is a pending TX (driver wrote register address via write()
    // without an explicit endTransmission()), use i2c_write_read so the
    // repeated-start path matches the typical "set register pointer then
    // read N bytes" sensor protocol.
    int rc;
    if (txLen > 0) {
        rc = i2c_write_read(dev, addr, txBuf, txLen, rxBuf, quantity);
        txLen = 0;
    } else {
        rc = i2c_read(dev, rxBuf, quantity, addr);
    }
    if (rc) {
        return 0;
    }
    rxLen = quantity;
    return quantity;
}

int TwoWire::available()
{
    return rxLen - rxPos;
}

int TwoWire::read()
{
    if (rxPos >= rxLen) {
        return -1;
    }
    return rxBuf[rxPos++];
}

int TwoWire::peek()
{
    if (rxPos >= rxLen) {
        return -1;
    }
    return rxBuf[rxPos];
}

size_t TwoWire::readBytes(uint8_t *buf, size_t len)
{
    size_t n = 0;
    while (n < len) {
        int b = read();
        if (b < 0) {
            break;
        }
        buf[n++] = (uint8_t)b;
    }
    return n;
}

TwoWire::operator bool() const
{
    return getI2CDevice() != nullptr;
}
