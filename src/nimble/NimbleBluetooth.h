#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : public BluetoothApi
{
  public:
    void setup() override;
    void shutdown() override;
    void deinit() override;
    void clearBonds() override;
    bool isActive();
    bool isConnected() override;
    int getRssi() override;
    void sendLog(const uint8_t *logMessage, size_t length) override;
#if defined(NIMBLE_TWO)
    void startAdvertising();
#endif
    bool isDeInit = false;

  private:
    void setupService();
#if !defined(NIMBLE_TWO)
    void startAdvertising();
#endif
};

void setBluetoothEnable(bool enable);
void clearNVS();