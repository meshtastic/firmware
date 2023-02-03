#pragma once

#include "BluetoothCommon.h"
#include <Arduino.h>

class NRF52Bluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void clearBonds();
    bool isConnected();
    int getRssi();

  private:
    static void onConnectionSecured(uint16_t conn_handle);
    void convertToUint8(uint8_t target[4], uint32_t source);
    static bool onPairingPasskey(uint16_t conn_handle, uint8_t const passkey[6], bool match_request);
    static void onPairingCompleted(uint16_t conn_handle, uint8_t auth_status);
};
