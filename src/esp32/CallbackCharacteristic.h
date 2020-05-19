#pragma once
#include "PowerFSM.h" // FIXME - someday I want to make this OTA thing a separate lb at at that point it can't touch this
#include "BLECharacteristic.h"

/**
 * This mixin just lets the power management state machine know the phone is still talking to us
 */
class BLEKeepAliveCallbacks : public BLECharacteristicCallbacks
{
public:
    void onRead(BLECharacteristic *c)
    {
        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);
    }

    void onWrite(BLECharacteristic *c)
    {
        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);
    }
};

/**
 * A characterstic with a set of overridable callbacks
 */
class CallbackCharacteristic : public BLECharacteristic, public BLEKeepAliveCallbacks
{
public:
    CallbackCharacteristic(const char *uuid, uint32_t btprops)
        : BLECharacteristic(uuid, btprops)
    {
        setCallbacks(this);
    }
};
