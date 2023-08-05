#pragma once
#include "BLECharacteristic.h"
#include "PowerFSM.h" // FIXME - someday I want to make this OTA thing a separate lb at at that point it can't touch this

/**
 * A characteristic with a set of overridable callbacks
 */
class CallbackCharacteristic : public BLECharacteristic, public BLECharacteristicCallbacks
{
  public:
    CallbackCharacteristic(const char *uuid, uint32_t btprops) : BLECharacteristic(uuid, btprops) { setCallbacks(this); }
};
