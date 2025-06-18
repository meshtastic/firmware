#include "ButtonThread.h"
#include "meshUtils.h"

#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "input/InputBroker.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include "sleep.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

using namespace concurrency;

#if HAS_BUTTON
#endif
ButtonThread::ButtonThread(const char *name) : OSThread(name)
{
    _originName = name;
}

bool ButtonThread::initButton(uint8_t pinNumber, bool activeLow, bool activePullup, uint32_t pullupSense, voidFuncPtr intRoutine,
                              input_broker_event singlePress, input_broker_event longPress, uint16_t longPressTime,
                              input_broker_event doublePress, input_broker_event longLongPress, uint16_t longLongPressTime,
                              input_broker_event triplePress, input_broker_event shortLong, bool touchQuirk)
{
    if (inputBroker)
        inputBroker->registerSource(this);
    _longPressTime = longPressTime;
    _longLongPressTime = longLongPressTime;
    _pinNum = pinNumber;
    _activeLow = activeLow;
    _touchQuirk = touchQuirk;
    _intRoutine = intRoutine;
    _longLongPress = longLongPress;

    userButton = OneButton(pinNumber, activeLow, activePullup);

    if (pullupSense != 0) {
        pinMode(pinNumber, pullupSense);
    }

    _singlePress = singlePress;
    userButton.attachClick(
        [](void *callerThread) -> void {
            ButtonThread *thread = (ButtonThread *)callerThread;
            thread->btnEvent = BUTTON_EVENT_PRESSED;
        },
        this);

    if (longPress != INPUT_BROKER_NONE) {
        _longPress = longPress;
        userButton.attachLongPressStart(
            [](void *callerThread) -> void {
                ButtonThread *thread = (ButtonThread *)callerThread;
                if (millis() > 30000) // hold off 30s after boot
                    thread->btnEvent = BUTTON_EVENT_LONG_PRESSED;
            },
            this);
        userButton.attachLongPressStop(
            [](void *callerThread) -> void {
                ButtonThread *thread = (ButtonThread *)callerThread;
                if (millis() > 30000) // hold off 30s after boot
                    thread->btnEvent = BUTTON_EVENT_LONG_RELEASED;
            },
            this);
    }

    if (doublePress != INPUT_BROKER_NONE) {
        _doublePress = doublePress;
        userButton.attachDoubleClick(
            [](void *callerThread) -> void {
                ButtonThread *thread = (ButtonThread *)callerThread;
                thread->btnEvent = BUTTON_EVENT_DOUBLE_PRESSED;
            },
            this);
    }

    if (triplePress != INPUT_BROKER_NONE) {
        _triplePress = triplePress;
        userButton.attachMultiClick(
            [](void *callerThread) -> void {
                ButtonThread *thread = (ButtonThread *)callerThread;
                thread->storeClickCount();
                thread->btnEvent = BUTTON_EVENT_MULTI_PRESSED;
            },
            this);
    }
    if (shortLong != INPUT_BROKER_NONE) {
        _shortLong = shortLong;
    }

    userButton.setDebounceMs(1);
    userButton.setPressMs(_longPressTime);

    if (screen) {
        userButton.setClickMs(20);
    } else {
        userButton.setClickMs(BUTTON_CLICK_MS);
    }
    attachButtonInterrupts();
#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    // Used to detach and reattach interrupts
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
    return true;
}

int32_t ButtonThread::runOnce()
{
    // If the button is pressed we suppress CPU sleep until release
    canSleep = true; // Assume we should not keep the board awake

    // Check for combination timeout
    if (waitingForLongPress && (millis() - shortPressTime) > BUTTON_COMBO_TIMEOUT_MS) {
        waitingForLongPress = false;
    }

    userButton.tick();
    canSleep &= userButton.isIdle();

    // Check if we should play lead-up sound during long press
    // Play lead-up when button has been held for BUTTON_LEADUP_MS but before long press triggers
    bool buttonCurrentlyPressed = isButtonPressed(_pinNum);

    // Detect start of button press
    if (buttonCurrentlyPressed && !buttonWasPressed) {
        buttonPressStartTime = millis();
        leadUpPlayed = false;
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    // Progressive lead-up sound system
    if (buttonCurrentlyPressed && (millis() - buttonPressStartTime) >= BUTTON_LEADUP_MS &&
        (millis() - buttonPressStartTime) < _longLongPressTime) {

        // Start the progressive sequence if not already active
        if (!leadUpSequenceActive) {
            leadUpSequenceActive = true;
            lastLeadUpNoteTime = millis();
            playNextLeadUpNote(); // Play the first note immediately
        }
        // Continue playing notes at intervals
        else if ((millis() - lastLeadUpNoteTime) >= 400) { // 400ms interval between notes
            if (playNextLeadUpNote()) {
                lastLeadUpNoteTime = millis();
            }
        }
    }

    // Reset when button is released
    if (!buttonCurrentlyPressed && buttonWasPressed) {
        leadUpPlayed = false;
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    buttonWasPressed = buttonCurrentlyPressed;

    // new behavior
    if (btnEvent != BUTTON_EVENT_NONE) {
        InputEvent evt;
        evt.source = _originName;
        evt.kbchar = 0;
        evt.touchX = 0;
        evt.touchY = 0;
        switch (btnEvent) {
        case BUTTON_EVENT_PRESSED: {
            // Forward single press to InputBroker (but NOT as DOWN/SELECT, just forward a "button press" event)
            evt.inputEvent = _singlePress;
            // evt.kbchar = _singlePress; // todo: fix this. Some events are kb characters rather than event types
            this->notifyObservers(&evt);

            // Start tracking for potential combination
            waitingForLongPress = true;
            shortPressTime = millis();

            break;
        }
        case BUTTON_EVENT_LONG_PRESSED: {
            // Ignore if: TX in progress
            // Uncommon T-Echo hardware bug, LoRa TX triggers touch button
            if (_touchQuirk && RadioLibInterface::instance && RadioLibInterface::instance->isSending())
                break;

            // Check if this is part of a short-press + long-press combination
            if (_shortLong != INPUT_BROKER_NONE && waitingForLongPress &&
                (millis() - shortPressTime) <= BUTTON_COMBO_TIMEOUT_MS) {
                evt.inputEvent = _shortLong;
                // evt.kbchar = _shortLong;
                this->notifyObservers(&evt);
                // Play the combination tune
                playComboTune();

                break;
            }

            // Forward long press to InputBroker (but NOT as DOWN/SELECT, just forward a "button long press" event)
            evt.inputEvent = _longPress;
            this->notifyObservers(&evt);

            // Reset combination tracking
            waitingForLongPress = false;

            break;
        }

        case BUTTON_EVENT_DOUBLE_PRESSED: { // not wired in if screen detected
            LOG_INFO("Double press!");

            // Reset combination tracking
            waitingForLongPress = false;

            evt.inputEvent = _doublePress;
            // evt.kbchar = _doublePress;
            this->notifyObservers(&evt);
            playComboTune();

            break;
        }

        case BUTTON_EVENT_MULTI_PRESSED: { // not wired in when screen is present
            LOG_INFO("Mulitipress! %hux", multipressClickCount);

            // Reset combination tracking
            waitingForLongPress = false;

            switch (multipressClickCount) {
            case 3:
                evt.inputEvent = _triplePress;
                // evt.kbchar = _triplePress;
                this->notifyObservers(&evt);
                playComboTune();
                break;

            // No valid multipress action
            default:
                break;
            } // end switch: click count

            break;
        } // end multipress event

            // Do actual shutdown when button released, otherwise the button release
        // may wake the board immediatedly.
        case BUTTON_EVENT_LONG_RELEASED: {

            LOG_INFO("LONG PRESS RELEASE");
            if (_longLongPress != INPUT_BROKER_NONE && (millis() - buttonPressStartTime) >= _longLongPressTime) {
                evt.inputEvent = _longLongPress;
                this->notifyObservers(&evt);
            }
            // Reset combination tracking
            waitingForLongPress = false;

            break;
        }
        }
    }
    btnEvent = BUTTON_EVENT_NONE;
    return 50;
}

/*
 * Attach (or re-attach) hardware interrupts for buttons
 * Public method. Used outside class when waking from MCU sleep
 */
void ButtonThread::attachButtonInterrupts()
{
    // Interrupt for user button, during normal use. Improves responsiveness.
    attachInterrupt(_pinNum, _intRoutine, CHANGE);
}

/*
 * Detach the "normal" button interrupts.
 * Public method. Used before attaching a "wake-on-button" interrupt for MCU sleep
 */
void ButtonThread::detachButtonInterrupts()
{
    detachInterrupt(_pinNum);
}

#ifdef ARCH_ESP32

// Detach our class' interrupts before lightsleep
// Allows sleep.cpp to configure its own interrupts, which wake the device on user-button press
int ButtonThread::beforeLightSleep(void *unused)
{
    detachButtonInterrupts();
    return 0; // Indicates success
}

// Reconfigure our interrupts
// Our class' interrupts were disconnected during sleep, to allow the user button to wake the device from sleep
int ButtonThread::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachButtonInterrupts();
    return 0; // Indicates success
}

#endif

// Non-static method, runs during callback. Grabs info while still valid
void ButtonThread::storeClickCount()
{
    multipressClickCount = userButton.getNumberClicks();
}