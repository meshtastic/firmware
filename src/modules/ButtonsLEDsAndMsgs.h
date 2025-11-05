#pragma once

#include "input/InputBroker.h"
#include "OneButton.h"
#include "concurrency/OSThread.h"
#include "configuration.h"


typedef void (*voidFuncPtr)(void);

struct ButtonConfigModules {
    uint8_t pinNumber;
    bool activeLow = true;
    bool activePullup = true;
    uint32_t pullupSense = 0;
    voidFuncPtr intRoutine = nullptr;
    // LED config for module-local LED control
    int ledPin = -1; // -1 = none
    bool ledActiveLow = true;
    input_broker_event singlePress = INPUT_BROKER_NONE;
    input_broker_event longPress = INPUT_BROKER_NONE;
    uint16_t longPressTime = 500;
    input_broker_event doublePress = INPUT_BROKER_NONE;
    input_broker_event longLongPress = INPUT_BROKER_NONE;
    uint16_t longLongPressTime = 3900;
    input_broker_event triplePress = INPUT_BROKER_NONE;
    input_broker_event shortLong = INPUT_BROKER_NONE;
    bool touchQuirk = false;
    // Channel index used for button-originated text messages (0 = public/broadcast)
    uint8_t channelIndex = 0xFF; // 0xFF = no channel configured

    // Constructor to set required parameter
    explicit ButtonConfigModules(uint8_t pin = 0) : pinNumber(pin) {}
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

class ButtonsLEDsAndMsgs : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    const char *_originName;
    static const uint32_t c_holdOffTime = 30000; // hold off 30s after boot
    bool initButton(const ButtonConfigModules &config);
    

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

    explicit ButtonsLEDsAndMsgs(const char *name);
    int32_t runOnce() override;
    // Old OneButton removed in favor of simple debounced edge detection
    // for maximal reliability of quick taps.
    // Observe input events from InputBroker (maps button-originated input to channel sends)
    int handleInputEvent(const InputEvent *event);
    void sendTextToChannel(const char *text, uint8_t channel);
    // Control the module-local LED (if configured)
    void setLed(bool on);
    void attachButtonInterrupts();
    void detachButtonInterrupts();
    void storeClickCount();
    // Treat any press type (short/long/double/multi) as the same action
    void triggerPressAction();
    bool isButtonPressed(int buttonPin)
    {
        if (_activeLow)
            return !digitalRead(buttonPin); // Active low: pressed = LOW
        else
            return digitalRead(buttonPin); // Most buttons are active low by default
    }

    // Returns true while this thread's button is physically held down
    bool isHeld() { return isButtonPressed(_pinNum); }

    // Return the configured GPIO pin number for this module's button
    int getPinNum() const { return _pinNum; }

    // Disconnect and reconnect interrupts for light sleep
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif
    // Receive incoming text messages so we can handle LED commands like "LED:1:ON"
    int handleTextMessage(const meshtastic_MeshPacket *mp);
    CallbackObserver<ButtonsLEDsAndMsgs, const meshtastic_MeshPacket *> textObserver =
        CallbackObserver<ButtonsLEDsAndMsgs, const meshtastic_MeshPacket *>(this, &ButtonsLEDsAndMsgs::handleTextMessage);
  private:
    input_broker_event _singlePress = INPUT_BROKER_NONE;
    input_broker_event _longPress = INPUT_BROKER_NONE;
    input_broker_event _longLongPress = INPUT_BROKER_NONE;

    input_broker_event _doublePress = INPUT_BROKER_NONE;
    input_broker_event _triplePress = INPUT_BROKER_NONE;
    input_broker_event _shortLong = INPUT_BROKER_NONE;

    voidFuncPtr _intRoutine = nullptr;
    uint16_t _longPressTime = 500;
    uint16_t _longLongPressTime = 3900;
    int _pinNum = 0;
    bool _activeLow = true;
    bool _touchQuirk = false;

    uint32_t buttonPressStartTime = 0;
    bool buttonWasPressed = false;

    // Timestamp of last sent button-originated packet (millis)
    uint32_t _lastSendMs = 0;

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<ButtonsLEDsAndMsgs, void *> lsObserver =
        CallbackObserver<ButtonsLEDsAndMsgs, void *>(this, &ButtonsLEDsAndMsgs::beforeLightSleep);
    CallbackObserver<ButtonsLEDsAndMsgs, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<ButtonsLEDsAndMsgs, esp_sleep_wakeup_cause_t>(this, &ButtonsLEDsAndMsgs::afterLightSleep);
#endif

    volatile ButtonEventType btnEvent = BUTTON_EVENT_NONE;

    // Store click count during callback, for later use
    volatile int multipressClickCount = 0;

    // Simple debounce state (polling-based)
    uint32_t _lastDebounceTime = 0;
    uint32_t _debounceMs = 30; // default debounce window
    bool _lastRawState = false;
    bool _stableState = false;

    // Combination tracking state
    bool waitingForLongPress = false;
    uint32_t shortPressTime = 0;

    // Long press lead-up tracking
    bool leadUpPlayed = false;
    uint32_t lastLeadUpNoteTime = 0;
    bool leadUpSequenceActive = false;

    static void wakeOnIrq(int irq, int mode);
    // Channel index used for button-originated text messages (0 = public/broadcast)
    uint8_t _channelIndex = 0;
    // LED control (module-local)
    int _ledPin = -1;
    bool _ledActiveLow = true;
    // Non-blocking LED off timestamp (millis), 0 when not scheduled
    uint32_t _ledOnUntil = 0;
    // Startup RGB blink state (moved from blocking init to non-blocking runOnce())
    bool _startupBlinkPending = false; // requested at init (if anyLed)
    bool _startupBlinkDone = false;    // finished
    uint8_t _startupBlinkPhase = 0;    // 0 = idle, 1 = on, 2 = off
    uint8_t _startupBlinkCount = 0;    // completed on/off cycles
    uint32_t _startupBlinkUntil = 0;   // next transition time
    // Note: We intentionally do not register with InputBroker; this module
    // handles button events locally and sends text messages itself.
    
};

extern ButtonsLEDsAndMsgs *buttonsLEDsAndMsgs;
