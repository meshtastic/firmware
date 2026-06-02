/*
  Bit-Bang HAL for RadioLib on ESP32-C6
  Bypasses hardware SPI completely. Extends LockingArduinoHal so it
  can be passed directly to initLoRa() / SX1262Interface constructor.

  Pinout: SCK=19(D8), MISO=20(D9), MOSI=18(D10), NSS=23(D5), BUSY=21(D3)
*/
#pragma once
#include <RadioLib.h>
#include "mesh/RadioLibInterface.h"

// Use real SPI but we override all its methods via our HAL
// (LockingArduinoHal constructor requires SPIClass&)

class BBTxRXHal : public LockingArduinoHal {
  uint8_t _sck, _miso, _mosi, _nss, _busy;

public:
  BBTxRXHal(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t nss, uint8_t busy)
    : LockingArduinoHal(SPI, SPISettings(1000000, MSBFIRST, SPI_MODE0)),
      _sck(sck), _miso(miso), _mosi(mosi), _nss(nss), _busy(busy)
  {
    // Configure bit-bang pins NOW (before any RadioLib init)
    ::pinMode(_nss,  OUTPUT); ::digitalWrite(_nss,  HIGH);
    ::pinMode(_sck,  OUTPUT); ::digitalWrite(_sck,  LOW);
    ::pinMode(_mosi, OUTPUT); ::digitalWrite(_mosi, LOW);
    ::pinMode(_miso, INPUT);
    if (_busy != 255) ::pinMode(_busy, INPUT); // RADIOLIB_NC = 255
  }

  // === Override ALL SPI methods to use bit-bang ===

  void init() override {
    // Don't call ArduinoHal::init() (would call SPI.begin())
    // Pins already configured in constructor
  }

  void term() override {
    // Nothing to tear down
  }

  void spiBegin() override {
    // Nothing - pins already configured
  }

  void spiBeginTransaction() override {
    // RadioLib calls digitalWrite(csPin, LOW) separately
    // No lock needed (ESP32-C6 is single core)
  }

  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    for (size_t i = 0; i < len; i++) {
      uint8_t tx = out ? out[i] : 0x00;
      uint8_t rx = 0;
      for (int b = 7; b >= 0; b--) {
        ::digitalWrite(_mosi, (tx >> b) & 1);
        delayMicroseconds(1);
        ::digitalWrite(_sck, HIGH);
        delayMicroseconds(1);
        if (::digitalRead(_miso)) rx |= (1 << b);
        ::digitalWrite(_sck, LOW);
        delayMicroseconds(1);
      }
      if (in) in[i] = rx;
    }
  }

  void spiEndTransaction() override {
    // RadioLib calls digitalWrite(csPin, HIGH) separately
  }

  void spiEnd() override {
    // Nothing
  }
};
