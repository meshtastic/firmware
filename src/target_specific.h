#pragma once

#include <Arduino.h>

// Functions that are unique to particular target types (esp32, bare, nrf52 etc...)

// Enable/disable bluetooth.
void setBluetoothEnable(bool enable);

void getMacAddr(uint8_t *dmac);

// Populate the caller-provided, zero-initialized 16-byte deviceId buffer with a stable,
// factory/silicon-derived hardware identifier (writes only the meaningful leading bytes;
// the caller pre-zeroes so any trailing bytes stay zero). Returns true if an id was
// written, or false if no stable id is available on this platform (buffer left unchanged,
// caller leaves MyNodeInfo.device_id unset). Re-read from silicon every boot, never
// persisted. Implemented per-architecture in src/platform/<arch>/.
bool getDeviceId(uint8_t *deviceId);
