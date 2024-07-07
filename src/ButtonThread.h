#pragma once

#include "OneButton.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#ifndef BUTTON_CLICK_MS
#define BUTTON_CLICK_MS 250
#endif

#ifndef BUTTON_LONGPRESS_MS
#define BUTTON_LONGPRESS_MS 5000
#endif

#ifndef BUTTON_TOUCH_MS
#define BUTTON_TOCH_MS 400
#endif

class ButtonThread : public concurrency::OSThread
{
  public:
    static const uint32_t c_holdOffTime = 30000; // hold off 30s after boot

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
    void attachButtonInterrupts();
    void detachButtonInterrupts();
    void storeClickCount();

  private:
#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO)
    static OneButton userButton; // Static - accessed from an interrupt
#endif
#ifdef BUTTON_PIN_ALT
    OneButton userButtonAlt;
#endif
#ifdef BUTTON_PIN_TOUCH
    OneButton userButtonTouch;
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

extern ButtonThread *buttonThread;
