#pragma once

#ifdef T_LORA_PAGER

// This is a version of RotaryEncoder which is based on a debounce inherent FSM table (see RotaryEncoder library)

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

class RotaryEncoder;

class TLoraPagerRotaryEncoder : public InputPollable
{
  public:
    TLoraPagerRotaryEncoder();
    bool init(void);
    virtual void pollOnce() override;

  protected:
    static TLoraPagerRotaryEncoder *interruptInstance;

    input_broker_event eventCw = INPUT_BROKER_NONE;
    input_broker_event eventCcw = INPUT_BROKER_NONE;
    input_broker_event eventPressed = INPUT_BROKER_NONE;

    RotaryEncoder *rotary;
};

extern TLoraPagerRotaryEncoder *tLoraPagerRotaryEncoder;

#endif
