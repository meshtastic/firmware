// NRF54L15Bluetooth.h — Zephyr BLE backend for nRF54L15
//
// Implements the same interface as NRF52Bluetooth (same method names and
// signatures) so main.cpp and AdminModule can use nrf52Bluetooth pointer
// without knowing the underlying implementation.
//
// GATT profile is identical to the nRF52 implementation:
//   Service:   MESH_SERVICE_UUID
//   toRadio:   TORADIO_UUID   (WRITE)
//   fromRadio: FROMRADIO_UUID (READ)
//   fromNum:   FROMNUM_UUID   (READ | NOTIFY)
//   logRadio:  LOGRADIO_UUID  (READ | NOTIFY | INDICATE)

#pragma once

#include "BluetoothCommon.h"

class NRF54L15Bluetooth : public BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void startDisabled();
    void resumeAdvertising();
    void clearBonds();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
};
