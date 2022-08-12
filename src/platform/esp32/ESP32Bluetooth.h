#ifdef USE_NEW_ESP32_BLUETOOTH

#pragma once

class ESP32Bluetooth
{
  public:
    void setup();
    void shutdown();
    void clearBonds();

  private:
    void setupService();
    void startAdvertising();
};

void setBluetoothEnable(bool on);
void clearNVS();
void disablePin();

#endif
