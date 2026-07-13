#pragma once

#include <Arduino.h>

// Functions that are unique to particular target types (esp32, bare, nrf52 etc...)

// Enable/disable bluetooth.
void setBluetoothEnable(bool enable);

void getMacAddr(uint8_t *dmac);

// Fill deviceId (a caller-zeroed 16-byte buffer) with a stable, silicon/factory-derived id and
// return true; return false (buffer untouched) when no stable id exists on this platform. Writes
// only the leading meaningful bytes, so the caller must pre-zero. Implemented per src/platform/<arch>/.
bool getDeviceId(uint8_t *deviceId);
