/**
 * Wire.h — Arduino Wire (I2C) shim for Zephyr/nRF54L15
 *
 * Provides the Arduino TwoWire interface expected by Meshtastic sensor
 * drivers and display libraries.
 *
 * Phase 2: compile-only stubs.  Phase 3: wire to Zephyr I2C API.
 */

#pragma once

#include "Arduino.h"
#include <stdint.h>

class TwoWire
{
  public:
    void begin() {}
    void begin(uint8_t sda, uint8_t scl) {}
    void begin(int sda, int scl, uint32_t freq) {}
    void end() {}
    void setClock(uint32_t freq) {}
    void setClockStretchLimit(uint32_t) {}

    void beginTransmission(uint8_t addr) {}
    void beginTransmission(int addr) { beginTransmission((uint8_t)addr); }
    uint8_t endTransmission(bool stop = true) { return 2; } // 2=NACK, no device at this address
    uint8_t endTransmission(uint8_t stop) { return 2; }

    uint8_t requestFrom(uint8_t addr, uint8_t quantity, bool stop = true) { return 0; }
    uint8_t requestFrom(uint8_t addr, uint8_t quantity, uint8_t stop) { return 0; }
    uint8_t requestFrom(int addr, int quantity, int stop = 1) { return 0; }

    size_t write(uint8_t data) { return 0; }
    size_t write(const uint8_t *data, size_t n) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    size_t readBytes(uint8_t *buf, size_t len) { return 0; }
    size_t readBytes(char *buf, size_t len) { return 0; }
    void flush() {}
    void onReceive(void (*)(int)) {}
    void onRequest(void (*)(void)) {}

    operator bool() const { return true; }
};

extern TwoWire Wire;
extern TwoWire Wire1;
