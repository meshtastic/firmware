// File: /master/FakeI2C.cpp

#include "FakeI2C.h"

#ifdef SENSECAP_INDICATOR

FakeI2C *FakeWire;

void FakeI2C::begin() {}

void FakeI2C::beginTransmission(uint8_t address) {
  _currentAddress = address;
  // create a default interdevice message for I2C start
  meshtastic_InterdeviceMessage cmd = meshtastic_InterdeviceMessage_init_default;
  cmd.which_data = meshtastic_InterdeviceMessage_i2c_command_tag;
  cmd.data.i2c_command.op = meshtastic_I2CCommand_Operation_START;
  cmd.data.i2c_command.addr = _currentAddress;
  sensecapIndicator->send_uplink(cmd);
}

void FakeI2C::endTransmission() {
 meshtastic_InterdeviceMessage cmd = meshtastic_InterdeviceMessage_init_default;
 cmd.which_data = meshtastic_InterdeviceMessage_i2c_command_tag;
  cmd.data.i2c_command.op = meshtastic_I2CCommand_Operation_STOP;
  sensecapIndicator->send_uplink(cmd);
}

void FakeI2C::write(uint8_t val) {
 meshtastic_InterdeviceMessage cmd = meshtastic_InterdeviceMessage_init_default;
 cmd.which_data = meshtastic_InterdeviceMessage_i2c_command_tag;
  cmd.data.i2c_command.op = meshtastic_I2CCommand_Operation_WRITE;
  cmd.data.i2c_command.data = val;
  sensecapIndicator->send_uplink(cmd);
}

uint8_t FakeI2C::requestFrom(uint8_t address, uint8_t quantity) {
  if (quantity != 1) return 0xFF;
 meshtastic_InterdeviceMessage cmd = meshtastic_InterdeviceMessage_init_default;
 cmd.which_data = meshtastic_InterdeviceMessage_i2c_command_tag;
  cmd.data.i2c_command.op = meshtastic_I2CCommand_Operation_READ;
  cmd.data.i2c_command.addr = address;
  cmd.data.i2c_command.ack = false;
  sensecapIndicator->send_uplink(cmd);
  // Wait for the response coming in asynchronously till there is a timeout

  unsigned long start = millis();
  while (millis() - start < 100) {
    if (_pending) {
      _pending = false; // Clear the pending flag
        return 1; // Indicate that we have read one byte
    }
    delay(10); // Avoid busy waiting
  }
  return 0;
  }

int FakeI2C::read() {
  return _lastByte;
}

uint8_t FakeI2C::readRegister(uint8_t reg) {
  beginTransmission(_currentAddress);
  write(reg);
  endTransmission();
  requestFrom(_currentAddress, 1);
  return read();
}

void FakeI2C::writeRegister(uint8_t reg, uint8_t val) {
  beginTransmission(_currentAddress);
  write(reg);
  write(val);
  endTransmission();
}

uint16_t FakeI2C::readRegister16(uint8_t reg) {
  beginTransmission(_currentAddress);
  write(reg);
  endTransmission();

  uint16_t result = 0;
  for (int i = 0; i < 2; i++) {
    requestFrom(_currentAddress, 1);
    result |= ((uint16_t)read()) << (8 * (1 - i));
  }
  return result;
}

void FakeI2C::writeRegister16(uint8_t reg, uint16_t val) {
  beginTransmission(_currentAddress);
  write(reg);
  write((uint8_t)(val >> 8));
  write((uint8_t)(val & 0xFF));
  endTransmission();
}

void FakeI2C::ingest(meshtastic_I2CResponse data) {
  // Simulate receiving data as if it were from an I2C device
  _lastByte = data.data; // Store the last byte read
  _pending = true; // Indicate that we have pending data
}

#endif // SENSECAP_INDICATOR