#ifndef USE_NEW_ESP32_BLUETOOTH

#pragma once

#include "PhoneAPI.h"

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

#endif //#ifndef USE_NEW_ESP32_BLUETOOTH