#pragma once

// This is a version of RotaryEncoder which is based on a debounce inherent FSM table (see RotaryEncoder library)

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

class RotaryEncoder;

class RotaryEncoderImpl : public InputPollable
{
  public:
    RotaryEncoderImpl();
    bool init(void);
    virtual void pollOnce() override;

  protected:
    static RotaryEncoderImpl *interruptInstance;

    input_broker_event eventCw = INPUT_BROKER_NONE;
    input_broker_event eventCcw = INPUT_BROKER_NONE;
    input_broker_event eventPressed = INPUT_BROKER_NONE;

    RotaryEncoder *rotary;
};

extern RotaryEncoderImpl *rotaryEncoderImpl;
