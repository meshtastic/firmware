/**
 * Wire.h - Arduino TwoWire (I2C) shim for Zephyr / nRF54L15.
 *
 * Bus binding: the Zephyr device tree alias `i2c30` (TWIM30 hardware
 * peripheral, HP domain, 3.0 V) is resolved at compile time via
 * DEVICE_DT_GET in Wire.cpp. SDA/SCL pins are configured in the board
 * overlay via pinctrl - `begin(sda, scl)` overloads are accepted for
 * Arduino API compatibility but the pin arguments are ignored.
 *
 * Buffer sizes are sized for the worst-case I2C consumer we plan to use
 * (NXP SE050 secure element, ~256-byte T=1 frames). BMP280 / INA228 /
 * SHT40 / INA3221 read in single-digit bytes and fit trivially.
 */

#pragma once

#include "Arduino.h"
#include <stddef.h>
#include <stdint.h>

#ifndef WIRE_BUFFER_LENGTH
#define WIRE_BUFFER_LENGTH 256
#endif

class TwoWire
{
  public:
    TwoWire();

    // ── Bus lifecycle ─────────────────────────────────────────────────
    // begin() variants - pin arguments are accepted for API compatibility
    // but ignored: SDA/SCL are fixed by the Zephyr overlay pinctrl. freq
    // is also fixed by overlay clock-frequency (use setClock() at runtime).
    void begin();
    void begin(uint8_t sda, uint8_t scl);
    void begin(int sda, int scl, uint32_t freq);
    void end();

    void setClock(uint32_t freq);
    void setClockStretchLimit(uint32_t) {} // no-op on TWIM hardware

    // ── Master write ─────────────────────────────────────────────────
    void beginTransmission(uint8_t addr);
    void beginTransmission(int addr) { beginTransmission((uint8_t)addr); }
    // Return codes (Arduino convention):
    //   0 = success, 1 = data-too-long, 2 = NACK on addr, 3 = NACK on data,
    //   4 = other error, 5 = timeout.
    uint8_t endTransmission(bool stop = true);
    uint8_t endTransmission(uint8_t stop) { return endTransmission(stop != 0); }

    size_t write(uint8_t data);
    size_t write(const uint8_t *data, size_t n);

    // ── Master read ──────────────────────────────────────────────────
    uint8_t requestFrom(uint8_t addr, uint8_t quantity, bool stop = true);
    uint8_t requestFrom(uint8_t addr, uint8_t quantity, uint8_t stop) { return requestFrom(addr, quantity, stop != 0); }
    uint8_t requestFrom(int addr, int quantity, int stop = 1) { return requestFrom((uint8_t)addr, (uint8_t)quantity, stop != 0); }

    int available();
    int read();
    int peek();
    size_t readBytes(uint8_t *buf, size_t len);
    size_t readBytes(char *buf, size_t len) { return readBytes((uint8_t *)buf, len); }
    void flush() {}

    // Slave callbacks unsupported - peripheral-only stub.
    void onReceive(void (*)(int)) {}
    void onRequest(void (*)(void)) {}

    operator bool() const;

  private:
    uint8_t txAddr;
    uint16_t txLen;
    uint8_t txBuf[WIRE_BUFFER_LENGTH];

    uint16_t rxLen;
    uint16_t rxPos;
    uint8_t rxBuf[WIRE_BUFFER_LENGTH];
};

extern TwoWire Wire;
extern TwoWire Wire1; // alias to Wire - only one I2C bus on this board
