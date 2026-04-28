#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : public BluetoothApi
{
  public:
    void setup() override;
    void shutdown() override;
    void deinit() override;
    void clearBonds() override;
    bool isActive() override;
    bool isConnected() override;
    int getRssi() override;
    void sendLog(const uint8_t *logMessage, size_t length) override;
    void startAdvertising();
    virtual ~NimbleBluetooth() {}

    bool isDeInit = false;

  private:
    void setupService();
};

void setBluetoothEnable(bool enable);