#pragma once

#include "configuration.h"
#include <Wire.h>
#include <Arduino.h>

#define MCP23017_KB_ADDR 0x20 // Default address (A0, A1 y A2 to GND)

class MCP23017Keyboard {
  public:
    typedef uint8_t (*i2c_com_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

    enum States { Init = 0, Idle, Held, HeldLong, Busy };

    States state;

    uint8_t last_key;
    uint32_t last_tap;
    uint8_t char_idx;

    String queue;

    MCP23017Keyboard();

    void begin(uint8_t addr = MCP23017_KB_ADDR, TwoWire *wire = &Wire);
    void begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr = MCP23017_KB_ADDR);

    void reset(void);

    void attachInterrupt(uint8_t pin, void (*func)(void)) const;
    void detachInterrupt(uint8_t pin) const;

    void trigger(void);
    void pressed(uint16_t value);
    void held(uint16_t value);
    void released(void);

    uint16_t status(void) const;
    uint8_t keyCount(void) const;
    uint8_t keyCount(uint16_t value) const;

    bool hasEvent(void);
    char dequeueEvent(void);
    void queueEvent(char);

    uint8_t readRegister8(uint8_t reg) const;
    uint16_t readRegister16(uint8_t reg) const;
    void writeRegister(uint8_t reg, uint8_t value);

  private:
    TwoWire *m_wire;
    uint8_t m_addr;
    i2c_com_fptr_t readCallback;
    i2c_com_fptr_t writeCallback;
};