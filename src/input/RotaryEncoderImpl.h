#pragma once

#ifdef T_LORA_PAGER

// This is a version of RotaryEncoder which is based on a debounce inherent FSM table (see RotaryEncoder library)

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

    QueueHandle_t inputQueue;
    void dispatchInputs(void);
    TaskHandle_t inputWorkerTask;
    static void inputWorker(void *p);
    EventGroupHandle_t  interruptFlag;
    static RotaryEncoderImpl* interruptInstance;

    input_broker_event eventCw = INPUT_BROKER_NONE;
    input_broker_event eventCcw = INPUT_BROKER_NONE;
    input_broker_event eventPressed = INPUT_BROKER_NONE;

    RotaryEncoder *rotary;
    const char *originName;
};

extern RotaryEncoderImpl *rotaryEncoderImpl;

#endif
