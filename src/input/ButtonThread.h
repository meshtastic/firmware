#pragma once

#include "InputBroker.h"
#include "OneButton.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

typedef void (*voidFuncPtr)(void);

struct ButtonConfig {
    uint8_t pinNumber;
    bool activeLow = true;
    bool activePullup = true;
    uint32_t pullupSense = 0;
    voidFuncPtr intRoutine = nullptr;
    input_broker_event singlePress = INPUT_BROKER_NONE;
    input_broker_event longPress = INPUT_BROKER_NONE;
    uint16_t longPressTime = 500;
    input_broker_event doublePress = INPUT_BROKER_NONE;
    input_broker_event longLongPress = INPUT_BROKER_NONE;
    uint16_t longLongPressTime = 3900;
    input_broker_event triplePress = INPUT_BROKER_NONE;
    input_broker_event shortLong = INPUT_BROKER_NONE;
    bool touchQuirk = false;

    // Constructor to set required parameter
    explicit ButtonConfig(uint8_t pin = 0) : pinNumber(pin) {}
};

#ifndef BUTTON_CLICK_MS
#define BUTTON_CLICK_MS 250
#endif

#ifndef BUTTON_TOUCH_MS
#define BUTTON_TOUCH_MS 400
#endif

#ifndef BUTTON_COMBO_TIMEOUT_MS
#define BUTTON_COMBO_TIMEOUT_MS 1000 // 1 second to complete the combination -- tap faster
#endif

#ifndef BUTTON_LEADUP_MS
#define BUTTON_LEADUP_MS 2200 // Play lead-up sound after 2.5 seconds of holding
#endif

class ButtonThread : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    const char *_originName;
    static const uint32_t c_holdOffTime = 30000; // hold off 30s after boot
    bool initButton(const ButtonConfig &config);

    enum ButtonEventType {
        BUTTON_EVENT_NONE,
        BUTTON_EVENT_PRESSED,
        BUTTON_EVENT_PRESSED_SCREEN,
        BUTTON_EVENT_DOUBLE_PRESSED,
        BUTTON_EVENT_MULTI_PRESSED,
        BUTTON_EVENT_LONG_PRESSED,
        BUTTON_EVENT_LONG_RELEASED,
        BUTTON_EVENT_TOUCH_LONG_PRESSED,
        BUTTON_EVENT_COMBO_SHORT_LONG,
    };

    explicit ButtonThread(const char *name);
    int32_t runOnce() override;
    OneButton userButton;
    void attachButtonInterrupts();
    void detachButtonInterrupts();
    void storeClickCount();
    bool isButtonPressed(int buttonPin)
    {
        if (_activeLow)
            return !digitalRead(buttonPin); // Active low: pressed = LOW
        else
            return digitalRead(buttonPin); // Most buttons are active low by default
    }

    // Disconnect and reconnect interrupts for light sleep
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif
  private:
    input_broker_event _singlePress = INPUT_BROKER_NONE;
    input_broker_event _longPress = INPUT_BROKER_NONE;
    input_broker_event _longLongPress = INPUT_BROKER_NONE;

    input_broker_event _doublePress = INPUT_BROKER_NONE;
    input_broker_event _triplePress = INPUT_BROKER_NONE;
    input_broker_event _shortLong = INPUT_BROKER_NONE;

    voidFuncPtr _intRoutine = nullptr;
    uint16_t _longPressTime = 500;
    uint16_t _longLongPressTime = 5000;
    int _pinNum = 0;
    bool _activeLow = true;
    bool _touchQuirk = false;

    uint32_t buttonPressStartTime = 0;
    bool buttonWasPressed = false;

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<ButtonThread, void *> lsObserver =
        CallbackObserver<ButtonThread, void *>(this, &ButtonThread::beforeLightSleep);
    CallbackObserver<ButtonThread, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<ButtonThread, esp_sleep_wakeup_cause_t>(this, &ButtonThread::afterLightSleep);
#endif

    volatile ButtonEventType btnEvent = BUTTON_EVENT_NONE;

    // Store click count during callback, for later use
    volatile int multipressClickCount = 0;

    // Combination tracking state
    bool waitingForLongPress = false;
    uint32_t shortPressTime = 0;

    // Long press lead-up tracking
    bool leadUpPlayed = false;
    uint32_t lastLeadUpNoteTime = 0;
    bool leadUpSequenceActive = false;

    static void wakeOnIrq(int irq, int mode);
};

extern ButtonThread *buttonThread;
