#pragma once
#include <Wire.h>

// Lightweight Wire-based TCA9555/PCA9555/XL9555 16-bit I/O expander driver.
// Opt in via USE_PCA95X5 / PCA95X5_CLS / PCA95X5_INC in the board's variant.h.
class Pca9555
{
  public:
    bool begin(TwoWire &wire, uint8_t addr, int sda = -1, int scl = -1)
    {
        _wire = &wire;
        _addr = addr;
        if (sda >= 0 && scl >= 0)
            wire.begin(sda, scl);
        wire.beginTransmission(addr);
        return wire.endTransmission() == 0;
    }

    void pinMode(int pin, int mode)
    {
        if (pin > 15 || !_wire)
            return;
        uint8_t port = pin / 8, bit = pin % 8;
        uint8_t cfg = readReg(0x06 + port);
        bool isInput = (mode == INPUT || mode == INPUT_PULLUP);
        if (isInput)
            cfg |= (1u << bit);
        else
            cfg &= ~(1u << bit);
        writeReg(0x06 + port, cfg);
    }

    void digitalWrite(int pin, int value)
    {
        if (pin > 15 || !_wire)
            return;
        uint8_t port = pin / 8, bit = pin % 8;
        uint8_t out = readReg(0x02 + port);
        if (value)
            out |= (1u << bit);
        else
            out &= ~(1u << bit);
        writeReg(0x02 + port, out);
    }

    bool digitalRead(int pin)
    {
        if (pin > 15 || !_wire)
            return false;
        uint8_t port = pin / 8, bit = pin % 8;
        return (readReg(0x00 + port) & (1u << bit)) != 0;
    }

  private:
    TwoWire *_wire = nullptr;
    uint8_t _addr = 0x20;

    uint8_t readReg(uint8_t reg)
    {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        if (_wire->endTransmission(false) != 0)
            return 0xFF;
        if (_wire->requestFrom((uint8_t)_addr, (uint8_t)1) != 1)
            return 0xFF;
        return _wire->read();
    }

    void writeReg(uint8_t reg, uint8_t val)
    {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }
};
