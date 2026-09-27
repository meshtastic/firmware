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

    bool pinMode(int pin, int mode)
    {
        if (pin < 0 || pin > 15 || !_wire)
            return false;
        uint8_t port = pin / 8, bit = pin % 8;
        uint8_t cfg;
        uint8_t res = readReg(0x06 + port, cfg);
        if (res) {
            bool isInput = (mode == INPUT || mode == INPUT_PULLUP);
            if (isInput)
                cfg |= (1u << bit);
            else
                cfg &= ~(1u << bit);
            return writeReg(0x06 + port, cfg);
        }
        return res;
    }

    bool digitalWrite(int pin, int value)
    {
        if (pin < 0 || pin > 15 || !_wire)
            return false;
        uint8_t port = pin / 8, bit = pin % 8;
        uint8_t out;
        bool res = readReg(0x02 + port, out);
        if (res) {
            if (value)
                out |= (1u << bit);
            else
                out &= ~(1u << bit);
            return writeReg(0x02 + port, out);
        }
        return res;
    }

    bool digitalRead(int pin)
    {
        if (pin < 0 || pin > 15 || !_wire)
            return false;
        uint8_t port = pin / 8, bit = pin % 8;
        uint8_t reg;
        if (readReg(0x00 + port, reg))
            return (reg & (1u << bit)) != 0;
        else
            return 0;
    }

  private:
    TwoWire *_wire = nullptr;
    uint8_t _addr = 0x20;

    bool readReg(uint8_t reg, uint8_t &out)
    {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        if (_wire->endTransmission(false) != 0) {
            _wire->end();
            _wire->begin();
            return false;
        }
        if (_wire->requestFrom((uint8_t)_addr, (uint8_t)1) != 1)
            return false;
        out = _wire->read();
        return true;
    }

    bool writeReg(uint8_t reg, uint8_t val)
    {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        _wire->write(val);
        if (_wire->endTransmission() != 0) {
            _wire->end();
            _wire->begin();
            return false;
        }
        return true;
    }
};
