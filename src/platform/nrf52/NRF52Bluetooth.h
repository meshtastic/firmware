#pragma once

#include "BluetoothCommon.h"
#include <Arduino.h>

// class BluetoothWrapper
// {
// public:
//   virtual void sendLog(const char *logMessage);
// };

class NRF52Bluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void startDisabled();
    void resumeAdvertising();
    void clearBonds();
    bool isConnected();
    int getRssi();
    void sendLog(const char *logMessage);

  private:
    static void onConnectionSecured(uint16_t conn_handle);
    static bool onPairingPasskey(uint16_t conn_handle, uint8_t const passkey[6], bool match_request);
    static void onPairingCompleted(uint16_t conn_handle, uint8_t auth_status);
};