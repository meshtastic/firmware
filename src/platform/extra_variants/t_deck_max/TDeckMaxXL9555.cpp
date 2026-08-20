#include "configuration.h"

#if defined(T_DECK_MAX)

#include "TDeckMaxXL9555.hpp"
#include "concurrency/LockGuard.h"

namespace
{
constexpr uint8_t PORT_MASK = 0xFF;
}

bool ExtensionIOXL9555::writeRegister(uint8_t reg, uint8_t value)
{
    if (!bus)
        return false;

    bus->beginTransmission(address);
    bus->write(reg);
    bus->write(value);
    return bus->endTransmission() == 0;
}

bool ExtensionIOXL9555::writePortPair(uint8_t reg, uint16_t value)
{
    if (!bus)
        return false;

    bus->beginTransmission(address);
    bus->write(reg);
    bus->write(static_cast<uint8_t>(value & PORT_MASK));
    bus->write(static_cast<uint8_t>((value >> 8) & PORT_MASK));
    return bus->endTransmission() == 0;
}

bool ExtensionIOXL9555::readPortPair(uint8_t reg, uint16_t &value)
{
    if (!bus)
        return false;

    bus->beginTransmission(address);
    bus->write(reg);
    if (bus->endTransmission() != 0)
        return false;

    if (bus->requestFrom(static_cast<int>(address), 2) != 2 || !bus->available())
        return false;
    const uint8_t low = static_cast<uint8_t>(bus->read());
    if (!bus->available())
        return false;
    const uint8_t high = static_cast<uint8_t>(bus->read());
    value = static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
    return true;
}

bool ExtensionIOXL9555::begin(TwoWire &wire, uint8_t newAddress, int sda, int scl)
{
    bus = &wire;
    address = newAddress;

    if (sda >= 0 && scl >= 0)
        bus->begin(sda, scl);

    bus->beginTransmission(address);
    if (bus->endTransmission() != 0) {
        ready = false;
        return false;
    }

    outputLatch = 0;
    direction = 0;
    ready = writePortPair(OUTPUT_PORT_0, outputLatch) && writePortPair(CONFIG_PORT_0, direction);
    return ready;
}

void ExtensionIOXL9555::pinMode(uint8_t pin, uint8_t mode)
{
    if (!ready || pin >= 16)
        return;

    concurrency::LockGuard guard(&lock);

    const uint16_t bit = static_cast<uint16_t>(1U << pin);
    if (mode == INPUT || mode == INPUT_PULLUP)
        direction |= bit;
    else
        direction &= static_cast<uint16_t>(~bit);

    writePortPair(CONFIG_PORT_0, direction);
}

void ExtensionIOXL9555::digitalWrite(uint8_t pin, uint8_t value)
{
    if (!ready || pin >= 16)
        return;

    concurrency::LockGuard guard(&lock);

    const uint16_t bit = static_cast<uint16_t>(1U << pin);
    if (value == HIGH)
        outputLatch |= bit;
    else
        outputLatch &= static_cast<uint16_t>(~bit);

    writePortPair(OUTPUT_PORT_0, outputLatch);
}

int ExtensionIOXL9555::digitalRead(uint8_t pin)
{
    if (!ready || pin >= 16)
        return LOW;

    concurrency::LockGuard guard(&lock);
    uint16_t input = 0;
    if (!readPortPair(INPUT_PORT_0, input))
        return LOW;
    return (input & static_cast<uint16_t>(1U << pin)) ? HIGH : LOW;
}

void ExtensionIOXL9555::setSafeState()
{
    if (!ready)
        return;

    concurrency::LockGuard guard(&lock);

    outputLatch = 0;
    direction = 0;
    writePortPair(OUTPUT_PORT_0, outputLatch);
    writePortPair(CONFIG_PORT_0, direction);
}

GpioPin *ExtensionIOXL9555::makeGpioPin(uint8_t pin)
{
    return new XL9555GpioPin(this, pin);
}

void XL9555GpioPin::set(bool value)
{
    owner->digitalWrite(pin, value ? HIGH : LOW);
}

#endif // T_DECK_MAX
