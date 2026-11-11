#pragma once

#include <Arduino.h>

// Functions that are unique to particular target types (esp32, bare, nrf52 etc...)

// Enable/disable bluetooth.
void setBluetoothEnable(bool enable);

void getMacAddr(uint8_t *dmac);

// Fill addr (6 bytes, most significant first) with the address the BLE stack advertises on; false if unknown here.
bool getBluetoothAddr(uint8_t *addr);

// Fill deviceId (a caller-zeroed 16-byte buffer) with a stable silicon/factory id; return true, or
// false (buffer untouched) if none exists here. Writes only leading bytes. Per-arch: src/platform/<arch>/.
bool getDeviceId(uint8_t *deviceId);
