
#pragma once
#include "BluetoothCommon.h"
#include "BluetoothSerial.h"

class BluetoothClassic : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
    bool isDeInit = false;

  private:
    void setupService();
};

void setBluetoothEnable(bool enable);

extern BluetoothClassic *bluetoothClassic;
