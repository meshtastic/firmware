#pragma once

#include <Arduino.h>
#include <functional>

/// We only allow one BLE connection at a time
extern int16_t curConnectionHandle;

// TODO(girts): create a class for the bluetooth utils helpers?
using StartBluetoothPinScreenCallback = std::function<void(uint32_t pass_key)>;
using StopBluetoothPinScreenCallback = std::function<void(void)>;

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level);
void deinitBLE();
void loopBLE();
void reinitBluetooth();

/**
 * A helper function that implements simple read and write handling for a uint32_t
 *
 * If a read, the provided value will be returned over bluetooth.  If a write, the value from the received packet
 * will be written into the variable.
 */
int chr_readwrite32le(uint32_t *v, struct ble_gatt_access_ctxt *ctxt);

/**
 * A helper for readwrite access to an array of bytes (with no endian conversion)
 */
int chr_readwrite8(uint8_t *v, size_t vlen, struct ble_gatt_access_ctxt *ctxt);