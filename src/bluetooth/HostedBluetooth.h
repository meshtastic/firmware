#pragma once

#include "BluetoothCommon.h"
#include <atomic>

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

    void setConnected(bool value);
    void setRssi(int value);

  private:
    void maybeLogFirstRssi(int value);
    bool registerCallbacks();
    void unregisterCallbacks();

    bool active = false;
    std::atomic<bool> connected{false};
    std::atomic<int> rssi{0};
    std::atomic<bool> firstRssiLogged{false};
    bool callbacksRegistered = false;
};
