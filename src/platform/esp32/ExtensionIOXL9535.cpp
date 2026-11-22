#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_XL9535)

#include "ExtensionIOXL9535.h"
#include "concurrency/LockGuard.h"

// Pins that must stay output-low (bit i = linear pin i): forced in every latch and configuration write
#ifndef XL9535_LOCKED_LOW_MASK
#define XL9535_LOCKED_LOW_MASK 0
#endif

#ifndef XL9535_I2C_TIMEOUT_MS
#define XL9535_I2C_TIMEOUT_MS 20
#endif

namespace
{
constexpr uint8_t XL9535_REG_INPUT0 = 0x00;  // 0x01 = port 1
constexpr uint8_t XL9535_REG_OUTPUT0 = 0x02; // 0x03 = port 1
constexpr uint8_t XL9535_REG_CONFIG0 = 0x06; // 0x07 = port 1, bit = 1 means input
constexpr uint8_t XL9535_MAX_ATTEMPTS = 3;

constexpr uint8_t lockedBits(uint8_t port)
{
    return (uint8_t)((uint16_t)(XL9535_LOCKED_LOW_MASK) >> (port * 8));
}
} // namespace

ExtensionIOXL9535 xl9535IoExpander;

// Every transfer below is one Arduino I2C HAL call: it takes the bus lock itself, so it is safe against
// other tasks using the same bus, and it fails cleanly while the bus is down.

bool ExtensionIOXL9535::probe()
{
    for (uint8_t attempt = 0; attempt < XL9535_MAX_ATTEMPTS; attempt++) {
        if (attempt)
            delay(2);
        if (i2cWrite(_bus, _addr, nullptr, 0, XL9535_I2C_TIMEOUT_MS) == ESP_OK)
            return true;
        _errors++;
    }
    return false;
}

bool ExtensionIOXL9535::readReg(uint8_t reg, uint8_t &val)
{
    uint8_t rx = 0;
    size_t got = 0;
    if (i2cWriteReadNonStop(_bus, _addr, &reg, 1, &rx, 1, XL9535_I2C_TIMEOUT_MS, &got) != ESP_OK || got != 1) {
        _errors++;
        return false;
    }
    val = rx;
    return true;
}

bool ExtensionIOXL9535::readRegRetry(uint8_t reg, uint8_t &val)
{
    for (uint8_t attempt = 0; attempt < XL9535_MAX_ATTEMPTS; attempt++) {
        if (readReg(reg, val))
            return true;
    }
    return false;
}

bool ExtensionIOXL9535::writeRegVerified(uint8_t reg, uint8_t val)
{
    const uint8_t tx[2] = {reg, val};
    for (uint8_t attempt = 0; attempt < XL9535_MAX_ATTEMPTS; attempt++) {
        if (i2cWrite(_bus, _addr, tx, sizeof(tx), XL9535_I2C_TIMEOUT_MS) != ESP_OK) {
            _errors++;
            continue;
        }
        uint8_t readBack = 0;
        if (!readReg(reg, readBack))
            continue;
        if (readBack == val)
            return true;
        _errors++;
    }
    return false;
}

bool ExtensionIOXL9535::applyLatch(uint8_t port)
{
    _out[port] &= (uint8_t)~lockedBits(port);
    _outVerified[port] = writeRegVerified(XL9535_REG_OUTPUT0 + port, _out[port]);
    return _outVerified[port];
}

bool ExtensionIOXL9535::applyConfig(uint8_t port)
{
    const uint8_t locked = lockedBits(port);
    uint8_t want = _cfg[port] & (uint8_t)~locked;
    bool ok = true;

    if (!_outVerified[port]) {
        // Unconfirmed latch: a pin that is an input now must not start driving an unknown level
        const uint8_t blocked = (uint8_t)~want & _cfgOnChip[port];
        if (blocked) {
            want |= blocked;
            ok = false;
        }
    }

    _cfg[port] = want;
    if (writeRegVerified(XL9535_REG_CONFIG0 + port, want)) {
        _cfgOnChip[port] = want;
    } else {
        _cfgOnChip[port] |= want; // old or new value may be in the chip: assume input where either says so
        ok = false;
    }

    if (locked) {
        uint8_t latch = 0;
        if (readReg(XL9535_REG_OUTPUT0 + port, latch) && (latch & locked)) {
            _errors++;
            if (!applyLatch(port)) {
                // Cannot get the locked pin low: release it (input) rather than leave it driven high
                _cfg[port] = want | locked;
                if (writeRegVerified(XL9535_REG_CONFIG0 + port, _cfg[port]))
                    _cfgOnChip[port] = _cfg[port];
                else
                    _cfgOnChip[port] |= _cfg[port];
                ok = false;
            }
        }
    }
    return ok;
}

bool ExtensionIOXL9535::begin(TwoWire &wire, uint8_t addr)
{
    concurrency::LockGuard guard(&_lock);
    if (_ready)
        return true;

    _bus = wire.getBusNum();
    _addr = addr;
    for (uint8_t port = 0; port < 2; port++) {
        _out[port] = 0xFF;
        _cfg[port] = 0xFF;
        _cfgOnChip[port] = 0xFF;
        _outVerified[port] = false;
    }

    if (!probe())
        return false;

    // The chip keeps its state across MCU resets: start from what it holds, not from power-on defaults
    for (uint8_t port = 0; port < 2; port++) {
        uint8_t val = 0xFF;
        if (readRegRetry(XL9535_REG_OUTPUT0 + port, val)) {
            _out[port] = val;
            _outVerified[port] = true;
        }
    }
    for (uint8_t port = 0; port < 2; port++) {
        uint8_t val = 0xFF;
        if (readRegRetry(XL9535_REG_CONFIG0 + port, val)) {
            _cfg[port] = val;
            _cfgOnChip[port] = val;
        }
    }

    // Locked pins: latch low and confirmed first, only then output (the power-on latch value is 1)
    for (uint8_t port = 0; port < 2; port++) {
        if (lockedBits(port) && !applyLatch(port))
            return false;
    }
    for (uint8_t port = 0; port < 2; port++) {
        if (lockedBits(port) && !applyConfig(port))
            return false;
    }

    _ready = true;
    return true;
}

bool ExtensionIOXL9535::isReady() const
{
    concurrency::LockGuard guard(&_lock);
    return _ready;
}

bool ExtensionIOXL9535::writePorts(uint16_t outputLatch, uint16_t outputMask)
{
    concurrency::LockGuard guard(&_lock);
    if (!_ready)
        return false;

    _out[0] = (uint8_t)(outputLatch & 0xFF);
    _out[1] = (uint8_t)(outputLatch >> 8);
    const bool latch0 = applyLatch(0);
    const bool latch1 = applyLatch(1);
    if (!latch0 || !latch1)
        return false; // no direction change on top of an unconfirmed latch

    _cfg[0] = (uint8_t)~(outputMask & 0xFF);
    _cfg[1] = (uint8_t)~(outputMask >> 8);
    const bool cfg0 = applyConfig(0);
    const bool cfg1 = applyConfig(1);
    return cfg0 && cfg1;
}

bool ExtensionIOXL9535::pinMode(uint8_t pin, uint8_t mode)
{
    concurrency::LockGuard guard(&_lock);
    if (!_ready || pin > 15)
        return false;

    const uint8_t port = pin >> 3;
    const uint8_t bit = (uint8_t)(1u << (pin & 7));
    if (mode == OUTPUT) {
        _cfg[port] &= (uint8_t)~bit;
    } else {
        if (lockedBits(port) & bit)
            return false;
        _cfg[port] |= bit;
    }
    return applyConfig(port);
}

bool ExtensionIOXL9535::digitalWrite(uint8_t pin, uint8_t value)
{
    concurrency::LockGuard guard(&_lock);
    if (!_ready || pin > 15)
        return false;

    const uint8_t port = pin >> 3;
    const uint8_t bit = (uint8_t)(1u << (pin & 7));
    if (value == LOW) {
        _out[port] &= (uint8_t)~bit;
    } else {
        if (lockedBits(port) & bit)
            return false;
        _out[port] |= bit;
    }
    return applyLatch(port);
}

int ExtensionIOXL9535::digitalRead(uint8_t pin)
{
    concurrency::LockGuard guard(&_lock);
    if (!_ready || pin > 15)
        return -1;

    uint8_t val = 0;
    if (!readReg(XL9535_REG_INPUT0 + (pin >> 3), val))
        return -1;
    return (val >> (pin & 7)) & 1;
}

bool ExtensionIOXL9535::readInputs(uint16_t &inputs)
{
    concurrency::LockGuard guard(&_lock);
    if (!_ready)
        return false;

    // Both ports are read even if the first fails: each read releases the INT cause of its own port
    uint8_t port0 = 0, port1 = 0;
    const bool ok0 = readReg(XL9535_REG_INPUT0, port0);
    const bool ok1 = readReg(XL9535_REG_INPUT0 + 1, port1);
    if (!ok0 || !ok1)
        return false;
    inputs = (uint16_t)(port0 | (port1 << 8));
    return true;
}

uint16_t ExtensionIOXL9535::outputShadow() const
{
    concurrency::LockGuard guard(&_lock);
    return (uint16_t)(_out[0] | (_out[1] << 8));
}

uint16_t ExtensionIOXL9535::outputMaskShadow() const
{
    concurrency::LockGuard guard(&_lock);
    return (uint16_t)~(_cfg[0] | (_cfg[1] << 8));
}

uint32_t ExtensionIOXL9535::errorCount() const
{
    concurrency::LockGuard guard(&_lock);
    return _errors;
}

#endif // ARCH_ESP32 && USE_XL9535
