#pragma once

// This is a non-interrupt version of RotaryEncoder which is based on a debounce inherent FSM table (see RotaryEncoder library)

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

class RotaryEncoder;

class RotaryEncoderImpl : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    RotaryEncoderImpl();
    bool init(void);

  protected:
    virtual int32_t runOnce() override;

    input_broker_event eventCw = INPUT_BROKER_NONE;
    input_broker_event eventCcw = INPUT_BROKER_NONE;
    input_broker_event eventPressed = INPUT_BROKER_NONE;

    RotaryEncoder *rotary;
    const char *originName;
};

extern RotaryEncoderImpl *rotaryEncoderImpl;
