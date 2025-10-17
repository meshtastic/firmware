#pragma once
#include "configuration.h"

#if defined(TTGO_T_ECHO) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

#include "concurrency/OSThread.h"

class TEchoBacklight : public concurrency::OSThread
{
  public:
    TEchoBacklight();
    int32_t runOnce() override;
    void setPin(uint8_t pin);
    void start();
    void peek();
    void latch();
    void off();

  private:
    static constexpr uint32_t LATCH_TIME_MS = 5000;
    static constexpr uint32_t POLL_INTERVAL_MS = 10;
    static constexpr uint32_t DEBOUNCE_MS = 50;
    static constexpr uint32_t BLINK_DELAY_MS = 25;
    static constexpr uint8_t BLINK_STEPS = 3;

    enum State { REST, IRQ, POLLING_UNFIRED, POLLING_FIRED, BLINKING };

    bool backlightLatched = false;
    uint32_t irqAtMillis = 0;
    State state = REST;
    uint32_t blinkStartTime = 0;
    uint8_t blinkStep = 0;

    void setBacklight(bool on);
    bool isTouchPressed();
    static void touchISR();
    void startThread();
    void stopThread();
};

extern TEchoBacklight *tEchoBacklight;

#endif
