#pragma once

#include "PhoneAPI.h"

extern uint16_t fromNumValHandle;

class BluetoothPhoneAPI : public PhoneAPI
{
protected: 
    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum);

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected();
};

extern PhoneAPI *bluetoothPhoneAPI;