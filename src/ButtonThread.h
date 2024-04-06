#pragma once

#include "OneButton.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

class ButtonThread : public concurrency::OSThread
{
  public:
    static const uint32_t c_longPressTime = 5000; // shutdown after 5s
    static const uint32_t c_holdOffTime = 30000;  // hold off 30s after boot

    enum ButtonEventType {
        BUTTON_EVENT_NONE,
        BUTTON_EVENT_PRESSED,
        BUTTON_EVENT_DOUBLE_PRESSED,
        BUTTON_EVENT_MULTI_PRESSED,
        BUTTON_EVENT_LONG_PRESSED,
        BUTTON_EVENT_LONG_RELEASED,
        BUTTON_EVENT_TOUCH_LONG_PRESSED,
    };

    ButtonThread();
    int32_t runOnce() override;
    void storeClickCount();

  private:
#ifdef BUTTON_PIN
    OneButton userButton;
#endif
#ifdef BUTTON_PIN_ALT
    OneButton userButtonAlt;
#endif
#ifdef BUTTON_PIN_TOUCH
    OneButton userButtonTouch;
#endif
#if defined(ARCH_PORTDUINO)
    OneButton userButton;
#endif

    // set during IRQ
    static volatile ButtonEventType btnEvent;

    // Store click count during callback, for later use
    volatile int multipressClickCount = 0;

    static void wakeOnIrq(int irq, int mode);

    // IRQ callbacks
    static void userButtonPressed() { btnEvent = BUTTON_EVENT_PRESSED; }
    static void userButtonDoublePressed() { btnEvent = BUTTON_EVENT_DOUBLE_PRESSED; }
    static void userButtonMultiPressed(void *callerThread); // Retrieve click count from non-static Onebutton while still valid
    static void userButtonPressedLongStart();
    static void userButtonPressedLongStop();
    static void touchPressedLongStart() { btnEvent = BUTTON_EVENT_TOUCH_LONG_PRESSED; }
};
