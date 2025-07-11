// File: /master/FakeI2C.h

#ifndef FAKEI2C_H
#define FAKEI2C_H

#ifdef SENSECAP_INDICATOR

#include <Arduino.h>
#include "../IndicatorSerial.h"
#include "../generated/meshtastic/interdevice.pb.h"

class FakeI2C {
public:
  void begin();
  void beginTransmission(uint8_t address);
  void endTransmission();
  void write(uint8_t val);
  uint8_t requestFrom(uint8_t address, uint8_t quantity);
  int read();

  uint8_t readRegister(uint8_t reg);
  void writeRegister(uint8_t reg, uint8_t val);
  uint16_t readRegister16(uint8_t reg);
  void writeRegister16(uint8_t reg, uint16_t val);

  void ingest(meshtastic_I2CResponse data);

private:
  uint8_t _currentAddress = 0;
  uint8_t _lastByte = 0;
  bool _pending = false; // Indicates if there is pending data to be read
};

extern FakeI2C *FakeWire;

#endif // SENSECAP_INDICATOR

#endif // FAKEI2C_H
