#ifdef USE_NEW_ESP32_BLUETOOTH

#pragma once

class ESP32Bluetooth
{
  public:
    void setup();
    void shutdown();
    void clearBonds();
};

void setBluetoothEnable(bool on);
void clearNVS();
void disablePin();

#endif