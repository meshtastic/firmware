/**
 * SPI.h — Arduino SPI shim for Zephyr/nRF54L15
 *
 * Provides the Arduino SPIClass interface.  The actual SPI bus is
 * configured via Zephyr DTS (SPIM20 in nrf54l15dk.overlay).
 * RadioLib uses its own SPI HAL so these stubs are only needed for
 * code that includes SPI.h directly without going through RadioLib.
 *
 * Phase 2: compile-only stubs.  Phase 3: wire to Zephyr SPI API.
 */

#pragma once

#include "Arduino.h"
#include <stdint.h>

#define SPI_MODE0  0
#define SPI_MODE1  1
#define SPI_MODE2  2
#define SPI_MODE3  3

struct SPISettings {
    uint32_t clock;
    uint8_t  bitOrder;
    uint8_t  dataMode;

    SPISettings(uint32_t clock = 4000000, uint8_t bitOrder = MSBFIRST, uint8_t dataMode = SPI_MODE0)
        : clock(clock), bitOrder(bitOrder), dataMode(dataMode) {}
};

class SPIClass {
  public:
    void begin()                                    {}
    void begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss = 0xFF) {}
    void end()                                      {}
    void beginTransaction(SPISettings)              {}
    void endTransaction()                           {}
    void setBitOrder(uint8_t order)                 {}
    void setDataMode(uint8_t mode)                  {}
    void setClockDivider(uint8_t div)               {}
    void setFrequency(uint32_t freq)                {}

    uint8_t  transfer(uint8_t data)                 { return 0; }
    uint16_t transfer16(uint16_t data)              { return 0; }
    void     transfer(void *buf, size_t count)      {}
    void     transferBytes(const uint8_t *tx, uint8_t *rx, uint32_t count) {}
    uint8_t  transfer(uint8_t tx, uint8_t *rx, uint32_t count) { return 0; }
};

extern SPIClass SPI;
extern SPIClass SPI1;
