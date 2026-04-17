/**
 * SPI.h — Arduino SPI shim for Zephyr/nRF54L15
 *
 * Provides the Arduino SPIClass interface backed by Zephyr's SPI API. The
 * backing controller is SPIM00 (HP domain, 3.0 V); the implementation in
 * nrf54l15_arduino.cpp binds to DEVICE_DT_GET(DT_NODELABEL(spi00)) and the
 * bus is configured in zephyr/boards/nrf54l15dk_nrf54l15_cpuapp.overlay.
 * RadioLib uses ArduinoHal which calls transfer() byte-by-byte.
 *
 * CS pin is handled by RadioLib via digitalWrite() — hardware CS is not used.
 */

#pragma once

#include "Arduino.h"
#include <stdint.h>

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

struct SPISettings {
    uint32_t clock;
    uint8_t bitOrder;
    uint8_t dataMode;

    SPISettings(uint32_t clock = 4000000, uint8_t bitOrder = MSBFIRST, uint8_t dataMode = SPI_MODE0)
        : clock(clock), bitOrder(bitOrder), dataMode(dataMode)
    {
    }
};

class SPIClass
{
  public:
    void begin() {}
    void begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss = 0xFF) {}
    void end() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    void setBitOrder(uint8_t order) {}
    void setDataMode(uint8_t mode) {}
    void setClockDivider(uint8_t div) {}
    void setFrequency(uint32_t freq) {}

    // Real Zephyr SPI implementation — defined in nrf54l15_arduino.cpp
    uint8_t transfer(uint8_t data);
    uint16_t transfer16(uint16_t data);
    void transfer(void *buf, size_t count);
    void transferBytes(const uint8_t *tx, uint8_t *rx, uint32_t count);
    uint8_t transfer(uint8_t tx, uint8_t *rx, uint32_t count)
    {
        transferBytes(&tx, rx, count);
        return rx ? rx[0] : 0;
    }
};

extern SPIClass SPI;
extern SPIClass SPI1;
