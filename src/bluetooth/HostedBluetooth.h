#pragma once

#include "BluetoothCommon.h"
#include <atomic>

/** Boards whose co-processor EN/reset line is not a host GPIO override this (variant.cpp).
 *  assertReset = true: hold the co-processor in reset. false: release it and return once it may be addressed.
 *  Return true when the variant drove the line; false falls back to the esp_hosted reset GPIO. */
bool variantHostedSlaveReset(bool assertReset);

/**
 * Placeholder backend for ESP32-P4 + ESP-Hosted BLE transport.
 *
 * This intentionally mirrors the NimbleBluetooth surface used by the firmware,
 * so the caller side can stay stable while we implement esp-hosted internals.
 */
class HostedBluetooth : public BluetoothApi
{
  public:
    HostedBluetooth();
    ~HostedBluetooth() override;

    void setup() override;
    void shutdown() override;
    void deinit() override;
    void clearBonds() override;
    bool isActive() override;
    bool isConnected() override;
    int getRssi() override;
    void sendLog(const uint8_t *logMessage, size_t length) override;
    void startAdvertising() override;

    void setConnected(bool value);
    void setRssi(int value);

  private:
    bool setupGatt();
    void shutdownGatt();
    void maybeLogFirstRssi(int value);
    bool registerCallbacks();
    void unregisterCallbacks();

    bool active = false;
    std::atomic<bool> connected{false};
    std::atomic<int> rssi{0};
    std::atomic<bool> firstRssiLogged{false};
    bool callbacksRegistered = false;
};
