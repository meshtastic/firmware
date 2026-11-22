#pragma once

#include "concurrency/Lock.h"
#include <Arduino.h>
#include <Wire.h>

/**
 * XL9535 16-bit I2C GPIO expander (PCA9555 register map). Linear pins 0-15 = P00..P07, P10..P17.
 *
 * Used by boards that route radio/display/power control lines through the expander (e.g. LilyGO
 * T-Display-P4). RadioLib access goes through XL9535LockingArduinoHal (virtual pins XL9535_VPIN_BASE..+15).
 * uint16_t arguments: bit i = linear pin i. Task context only, and no logging (runs before the console is up).
 */
class ExtensionIOXL9535
{
  public:
    /** Probe the chip and load the shadows. The caller has begun the bus; only wire.getBusNum() is kept. */
    bool begin(TwoWire &wire, uint8_t addr);
    /** True after a successful begin(). Until then every other method fails without I2C traffic. */
    bool isReady() const;

    /** Set all 16 pins: output latches first (verified), then directions. outputMask bit = 1 means output. */
    bool writePorts(uint16_t outputLatch, uint16_t outputMask);
    /** OUTPUT -> output, anything else -> input. A pin only becomes an output on top of a verified latch. */
    bool pinMode(uint8_t pin, uint8_t mode);
    /** Writes the output latch bit, whatever the direction of the pin. */
    bool digitalWrite(uint8_t pin, uint8_t value);
    /** 0 / 1 from the input register, -1 on error. */
    int digitalRead(uint8_t pin);
    /** Reads both input ports (which also releases the INT output). */
    bool readInputs(uint16_t &inputs);

    uint16_t outputShadow() const;     // bit i = latch of pin i
    uint16_t outputMaskShadow() const; // bit i = 1 when pin i is an output
    uint32_t errorCount() const;       // failed I2C transactions since boot

  private:
    // Unlocked helpers: the lock is not recursive, so public methods take it once and only call these.
    bool probe();
    bool readReg(uint8_t reg, uint8_t &val);
    bool readRegRetry(uint8_t reg, uint8_t &val);
    bool writeRegVerified(uint8_t reg, uint8_t val);
    bool applyLatch(uint8_t port);
    bool applyConfig(uint8_t port);

    uint8_t _bus = 0;
    uint8_t _addr = 0;
    bool _ready = false;
    uint8_t _out[2] = {0xFF, 0xFF};        // intended output latches (power-on default)
    uint8_t _cfg[2] = {0xFF, 0xFF};        // intended configuration, 1 = input (power-on default)
    uint8_t _cfgOnChip[2] = {0xFF, 0xFF};  // configuration known to be in the chip, 1 where unsure
    bool _outVerified[2] = {false, false}; // last latch write of the port was confirmed by read-back
    uint32_t _errors = 0;
    mutable concurrency::Lock _lock;
};

/** Global instance shared by the variant hooks and the RadioLib HAL. */
extern ExtensionIOXL9535 xl9535IoExpander;
