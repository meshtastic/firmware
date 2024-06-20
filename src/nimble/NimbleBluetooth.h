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
    void sendLog(const char *logMessage);

  private:
    void setupService();
    void startAdvertising();
};

void setBluetoothEnable(bool enable);
void clearNVS();