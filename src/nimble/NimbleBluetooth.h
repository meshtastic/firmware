#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    // The PIN pairing asks for; false in NO_PIN mode or before setup()
    bool getPasskey(uint32_t *pin);
    void sendLog(const uint8_t *logMessage, size_t length);
    void startAdvertising();
    bool isDeInit = false;

  private:
    void setupService();
};

void setBluetoothEnable(bool enable);