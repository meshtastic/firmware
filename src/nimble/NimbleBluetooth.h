#pragma once

class NimbleBluetooth
{
  public:
    void setup();
    void shutdown();
    void clearBonds();
    bool isActive();

  private:
    void setupService();
    void startAdvertising();
};

void setBluetoothEnable(bool on);
void clearNVS();
void disablePin();
