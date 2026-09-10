#pragma once
#include "TouchScreenBase.h"

class TouchScreenImpl1 : public TouchScreenBase
{
  public:
    TouchScreenImpl1(uint16_t width, uint16_t height, bool (*getTouch)(int16_t *, int16_t *));
    void init(void);

  protected:
    virtual bool getTouch(int16_t &x, int16_t &y);
    virtual void onEvent(const TouchEvent &event);
    bool fastTapModeEnabled() const override;
    bool longPressEnabled() const override;

    // Attach/detach a hardware interrupt on the touch IRQ pin (SCREEN_TOUCH_INT) so a new touch
    // wakes the polling thread immediately. No-op on boards without a usable touch interrupt line.
    void attachTouchInterrupt();

    bool (*_getTouch)(int16_t *, int16_t *);

#ifdef ARCH_ESP32
    // Detach the touch interrupt before light sleep (so sleep.cpp can own the wake config),
    // and reattach it afterwards. Mirrors ButtonThread's interrupt handling.
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);

    CallbackObserver<TouchScreenImpl1, void *> lsObserver =
        CallbackObserver<TouchScreenImpl1, void *>(this, &TouchScreenImpl1::beforeLightSleep);
    CallbackObserver<TouchScreenImpl1, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<TouchScreenImpl1, esp_sleep_wakeup_cause_t>(this, &TouchScreenImpl1::afterLightSleep);
#endif
};

extern TouchScreenImpl1 *touchScreenImpl1;
