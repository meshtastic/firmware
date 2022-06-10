#ifdef USE_NEW_ESP32_BLUETOOTH

#pragma once

extern uint16_t fromNumValHandle;

class BluetoothPhoneAPI : public PhoneAPI
{
protected: 
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) override;

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;
};

extern PhoneAPI *bluetoothPhoneAPI;

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