#pragma once

#include "concurrency/Lock.h"
#include <Arduino.h>
#include <Wire.h>

/**
 * MCP23017 16-bit I2C GPIO expander (pins 0-15 = GPA0-7, GPB0-7).
 *
 * Used by boards that route radio/display control lines through the expander
 * (e.g. Meshnology W10). RadioLib access goes through MCP23017LockingArduinoHal,
 * which maps virtual pins MCP23017_VPIN_BASE..+15 onto local pins 0-15.
 */
class ExtensionIOMCP23017
{
  public:
    ExtensionIOMCP23017() : _wire(nullptr), _addr(0), _begun(false) {}

    void begin(TwoWire &wire, uint8_t addr, int sda, int scl);

    /** Local pin index 0-15 (GPA0=0 ... GPB7=15), not the virtual RadioLib pin. */
    void pinMode(uint8_t pin, uint8_t mode);
    void digitalWrite(uint8_t pin, uint8_t value);
    int digitalRead(uint8_t pin);

    /** Enable the MCP23017 pin-change interrupt so /INT asserts (only useful if /INT is wired to the MCU). */
    void enablePinChangeInterrupt(uint8_t pin, bool enable);

    /** Read INTCAPx to clear a latched interrupt condition after MCP /INT asserts. */
    void clearInterruptLatches();

    /** Raw register read (0x00-0x1A), for bring-up / debug. */
    uint8_t readRegister(uint8_t reg);

  private:
    uint8_t readReg(uint8_t reg);
    // Checked read: returns false (and leaves val untouched) on any I2C error, so a glitched read
    // can't drive a read-modify-write that clobbers the rest of the bank.
    bool readReg(uint8_t reg, uint8_t &val);
    void writeReg(uint8_t reg, uint8_t val);
    void updateDirectionBit(uint8_t pin, bool asOutput);
    uint8_t iodirRegForPin(uint8_t pin) const { return pin < 8 ? 0x00 : 0x01; }
    uint8_t gpioRegForPin(uint8_t pin) const { return pin < 8 ? 0x12 : 0x13; }
    uint8_t olatRegForPin(uint8_t pin) const { return pin < 8 ? 0x14 : 0x15; }
    uint8_t gppuRegForPin(uint8_t pin) const { return pin < 8 ? 0x0C : 0x0D; }
    uint8_t gpintenRegForPin(uint8_t pin) const { return pin < 8 ? 0x04 : 0x05; }
    uint8_t intconRegForPin(uint8_t pin) const { return pin < 8 ? 0x08 : 0x09; }
    uint8_t bitForPin(uint8_t pin) const { return 1u << (pin & 7); }

    TwoWire *_wire;
    uint8_t _addr;
    bool _begun;
    // Serializes register access: the radio HAL (BUSY/DIO1/RESET) and AudioThread (amp enable) both
    // reach this expander from different threads, and the read-modify-write paths are not atomic.
    concurrency::Lock _lock;
};

/** Global instance shared by the early-init hook and the RadioLib HAL. */
extern ExtensionIOMCP23017 mcpIoExpander;

/**
 * Bring up the expander and set board-specific pin directions/levels (LoRa reset,
 * LCD reset, GPS wake, ...). Must run after power->setup() so the PMU rails are up,
 * and before the I2C scan / radio / display init.
 */
void mcp23017EarlyInit();
