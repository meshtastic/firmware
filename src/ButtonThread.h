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
#define BUTTON_TOUCH_MS 400
#endif

extern int screen_flag;

class ButtonThread : public concurrency::OSThread
{
  public:
    static const uint32_t c_holdOffTime = 30000; // hold off 30s after boot

    enum ButtonEventType {
        BUTTON_EVENT_NONE,
        BUTTON_EVENT_PRESSED,
        BUTTON_EVENT_PRESSED1,
        BUTTON_EVENT_DOUBLE_PRESSED,
        BUTTON_EVENT_DOUBLE_PRESSED1,
        BUTTON_EVENT_MULTI_PRESSED,
        BUTTON_EVENT_LONG_PRESSED,
        BUTTON_EVENT_LONG_PRESSED1,
        BUTTON_EVENT_LONG_RELEASED,
        BUTTON_EVENT_TOUCH_LONG_PRESSED,
    };

    ButtonThread();
    int32_t runOnce() override;
    void attachButtonInterrupts();
    void detachButtonInterrupts();
    void storeClickCount();

    bool BEEP_STATE = false;
    void buzzer_updata();
    void buzzer_test();
    enum BuzzerState { OFF, SHORT_BEEP, LONG_BEEP, SHORT_BEEP1, WAIT };

    bool buzzer_flag = false;
    BuzzerState currentState = OFF;
    const int beepDuration = 1000;     // 发声持续时间
    const int pauseDuration = 1000;    // 间隔时间
    const int longBeepDuration = 3000; // 长音持续时间
    unsigned long stateStartTime = 0;
    bool isBuzzing = false; // 蜂鸣器状态
    int cont = 0;
    // Disconnect and reconnect interrupts for light sleep
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif
  private:
#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO) || defined(USERPREFS_BUTTON_PIN)
    static OneButton userButton; // Static - accessed from an interrupt
#endif
#ifdef BUTTON_PIN_ALT
    OneButton userButtonAlt;
#endif
#ifdef BUTTON_PIN_TOUCH
    OneButton userButtonTouch;
#endif

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<ButtonThread, void *> lsObserver =
        CallbackObserver<ButtonThread, void *>(this, &ButtonThread::beforeLightSleep);
    CallbackObserver<ButtonThread, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<ButtonThread, esp_sleep_wakeup_cause_t>(this, &ButtonThread::afterLightSleep);
#endif

    // set during IRQ
    static volatile ButtonEventType btnEvent;

    // Store click count during callback, for later use
    volatile int multipressClickCount = 0;

    static void wakeOnIrq(int irq, int mode);

    // IRQ callbacks
    static void userButtonPressed() { btnEvent = BUTTON_EVENT_PRESSED; }
    static void userButtonPressed1()
    {
        if (millis() > c_holdOffTime) {
            btnEvent = BUTTON_EVENT_PRESSED1;
        }
    }
    static void userButtonDoublePressed() { btnEvent = BUTTON_EVENT_DOUBLE_PRESSED; }
    static void userButtonDoublePressed1() { btnEvent = BUTTON_EVENT_DOUBLE_PRESSED1; }
    static void userButtonMultiPressed(void *callerThread); // Retrieve click count from non-static Onebutton while still valid
    static void userButtonPressedLongStart();
    static void userButtonPressedLongStart1();
    static void userButtonPressedLongStop();
    static void touchPressedLongStart() { btnEvent = BUTTON_EVENT_TOUCH_LONG_PRESSED; }
};

extern ButtonThread *buttonThread;
