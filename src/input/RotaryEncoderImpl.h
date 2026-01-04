#pragma once

// This is a version of RotaryEncoder which is based on a debounce inherent FSM table (see RotaryEncoder library)

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

class RotaryEncoder;

class RotaryEncoderImpl final : public InputPollable
{
  public:
    RotaryEncoderImpl();
    ~RotaryEncoderImpl() override;
    bool init();
    virtual void pollOnce() override;
    // Disconnect and reconnect interrupts for light sleep
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif

  protected:
    static RotaryEncoderImpl *interruptInstance;

    input_broker_event eventCw = INPUT_BROKER_NONE;
    input_broker_event eventCcw = INPUT_BROKER_NONE;
    input_broker_event eventPressed = INPUT_BROKER_NONE;

    RotaryEncoder *rotary;

  private:
#ifdef ARCH_ESP32
    bool isFirstInit;
#endif
    void detachRotaryEncoderInterrupts();
    void attachRotaryEncoderInterrupts();

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<RotaryEncoderImpl, void *> lsObserver =
        CallbackObserver<RotaryEncoderImpl, void *>(this, &RotaryEncoderImpl::beforeLightSleep);
    CallbackObserver<RotaryEncoderImpl, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<RotaryEncoderImpl, esp_sleep_wakeup_cause_t>(this, &RotaryEncoderImpl::afterLightSleep);
#endif
};

extern RotaryEncoderImpl *rotaryEncoderImpl;
