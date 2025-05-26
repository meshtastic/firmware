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
    void sendLog(const uint8_t *logMessage, size_t length);
    void Send_GPWPL(uint32_t node, char* name,int32_t latitude_i,int32_t longitude_i);

  private:
    void setupService();
    void startAdvertising();
};

void setBluetoothEnable(bool enable);
void clearNVS();