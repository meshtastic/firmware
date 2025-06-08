#include "ButtonThread.h"

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

#define DEBUG_BUTTONS 1
#if DEBUG_BUTTONS
#define LOG_BUTTON(...) LOG_DEBUG(__VA_ARGS__)
#else
#define LOG_BUTTON(...)
#endif

using namespace concurrency;

volatile ButtonThread::ButtonEventType ButtonThread::btnEvent = ButtonThread::BUTTON_EVENT_NONE;

#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO) || defined(USERPREFS_BUTTON_PIN)
OneButton ButtonThread::userButton; // Get reference to static member
#endif
ButtonThread::ButtonThread(const char *name) : OSThread(name)
{
    _originName = name;
#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO) || defined(USERPREFS_BUTTON_PIN)

#if defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC) {
        this->userButton = OneButton(settingsMap[user], true, true);
        LOG_DEBUG("Use GPIO%02d for button", settingsMap[user]);
    }
#elif defined(BUTTON_PIN)
#if !defined(USERPREFS_BUTTON_PIN)
    int pin = config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN;           // Resolved button pin
#endif
#ifdef USERPREFS_BUTTON_PIN
    int pin = config.device.button_gpio ? config.device.button_gpio : USERPREFS_BUTTON_PIN; // Resolved button pin
#endif
#if defined(HELTEC_CAPSULE_SENSOR_V3) || defined(HELTEC_SENSOR_HUB)
    this->userButton = OneButton(pin, false, false);
#elif defined(BUTTON_ACTIVE_LOW)
    userButton = OneButton(pin, BUTTON_ACTIVE_LOW, BUTTON_ACTIVE_PULLUP);
#else
    this->userButton = OneButton(pin, true, true);
#endif
    LOG_DEBUG("Use GPIO%02d for button", pin);
#endif

#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
#ifdef BUTTON_SENSE_TYPE
    pinMode(pin, BUTTON_SENSE_TYPE);
#else
    pinMode(pin, INPUT_PULLUP_SENSE);
#endif
#endif

#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO) || defined(USERPREFS_BUTTON_PIN)
    userButton.attachClick([]() { btnEvent = BUTTON_EVENT_PRESSED; });

    userButton.setDebounceMs(1);
    if (screen) {
        userButton.setClickMs(20);
        userButton.setPressMs(500);
    } else {
        userButton.setPressMs(BUTTON_LONGPRESS_MS);
        userButton.setClickMs(BUTTON_CLICK_MS);
        userButton.attachDoubleClick(userButtonDoublePressed);
        userButton.attachMultiClick(userButtonMultiPressed,
                                    this); // Reference to instance: get click count from non-static OneButton
    }
#if !defined(ELECROW_ThinkNode_M2) // T-Deck immediately wakes up after shutdown, Thinknode M2 has this on the smaller ALT button
    userButton.attachLongPressStart(userButtonPressedLongStart);
    userButton.attachLongPressStop(userButtonPressedLongStop);
#endif
#endif

#ifdef BUTTON_PIN_ALT
#if defined(ELECROW_ThinkNode_M2)
    this->userButtonAlt = OneButton(BUTTON_PIN_ALT, false, false);
#else
    this->userButtonAlt = OneButton(BUTTON_PIN_ALT, true, true);
#endif
#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
    pinMode(BUTTON_PIN_ALT, INPUT_PULLUP_SENSE);
#endif
    userButtonAlt.attachClick(userButtonPressedScreen);
    userButtonAlt.setClickMs(BUTTON_CLICK_MS);
    userButtonAlt.setPressMs(BUTTON_LONGPRESS_MS);
    userButtonAlt.setDebounceMs(1);
    userButtonAlt.attachLongPressStart(userButtonPressedLongStart);
    userButtonAlt.attachLongPressStop(userButtonPressedLongStop);
#endif

#ifdef BUTTON_PIN_TOUCH
    userButtonTouch = OneButton(BUTTON_PIN_TOUCH, true, true);
    userButtonTouch.setPressMs(BUTTON_TOUCH_MS);
    userButtonTouch.attachLongPressStart(touchPressedLongStart); // Better handling with longpress than click?
#endif

#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    // Used to detach and reattach interrupts
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif

    attachButtonInterrupts();
#endif
}

void ButtonThread::sendAdHocPosition()
{
    service->refreshLocalMeshNode();
    service->trySendPosition(NODENUM_BROADCAST, true);
    playComboTune();
}

int32_t ButtonThread::runOnce()
{
    // If the button is pressed we suppress CPU sleep until release
    canSleep = true; // Assume we should not keep the board awake

#if defined(BUTTON_PIN) || defined(USERPREFS_BUTTON_PIN)
    userButton.tick();
    canSleep &= userButton.isIdle();
#elif defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC) {
        userButton.tick();
        canSleep &= userButton.isIdle();
    }
#endif
#ifdef BUTTON_PIN_ALT
    userButtonAlt.tick();
    canSleep &= userButtonAlt.isIdle();
#endif
#ifdef BUTTON_PIN_TOUCH
    userButtonTouch.tick();
    canSleep &= userButtonTouch.isIdle();
#endif

    // Check for combination timeout
    if (waitingForLongPress && (millis() - shortPressTime) > BUTTON_COMBO_TIMEOUT_MS) {
        waitingForLongPress = false;
    }

    // Check if we should play lead-up sound during long press
    // Play lead-up when button has been held for BUTTON_LEADUP_MS but before long press triggers
#if defined(BUTTON_PIN) || defined(USERPREFS_BUTTON_PIN) || defined(ARCH_PORTDUINO)
    bool buttonCurrentlyPressed = false;
#if defined(BUTTON_PIN) || defined(USERPREFS_BUTTON_PIN)
    // Read the actual physical state of the button pin
#if !defined(USERPREFS_BUTTON_PIN)
    int buttonPin = config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN;
#else
    int buttonPin = config.device.button_gpio ? config.device.button_gpio : USERPREFS_BUTTON_PIN;
#endif
    buttonCurrentlyPressed = isButtonPressed(buttonPin);
#elif defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC) {
        // For portduino, assume active low
        buttonCurrentlyPressed = isButtonPressed(settingsMap[user]);
    }
#endif

    static uint32_t buttonPressStartTime = 0;
    static bool buttonWasPressed = false;

    // Detect start of button press
    if (buttonCurrentlyPressed && !buttonWasPressed) {
        buttonPressStartTime = millis();
        leadUpPlayed = false;
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    // Progressive lead-up sound system
    if (buttonCurrentlyPressed && (millis() - buttonPressStartTime) >= BUTTON_LEADUP_MS &&
        (millis() - buttonPressStartTime) < BUTTON_LONGPRESS_MS) {

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
#endif

    if (btnEvent != BUTTON_EVENT_NONE) {
        switch (btnEvent) {
        case BUTTON_EVENT_PRESSED: {
            LOG_WARN("press!");

            // Play boop sound for every button press
            playBoop();

            // Forward single press to InputBroker (but NOT as DOWN/SELECT, just forward a "button press" event)
            InputEvent evt;
            evt.source = _originName;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
            evt.inputEvent = INPUT_BROKER_USER_PRESS;
            this->notifyObservers(&evt);

            // Start tracking for potential combination
            if (!screen) {
                waitingForLongPress = true;
                shortPressTime = millis();
            }
            break;
        }

        case BUTTON_EVENT_PRESSED_SCREEN: {
            LOG_BUTTON("AltPress!");

            // Play boop sound for every button press
            playBoop();

            // Reset combination tracking
            waitingForLongPress = false;

            break;
        }

        case BUTTON_EVENT_DOUBLE_PRESSED: { // not wired in if screen detected
            LOG_BUTTON("Double press!");

            // Play boop sound for every button press
            playBoop();

            // Reset combination tracking
            waitingForLongPress = false;

            // Send GPS position immediately
            sendAdHocPosition();

            break;
        }

        case BUTTON_EVENT_MULTI_PRESSED: { // not wired in when screen is present
            LOG_BUTTON("Mulitipress! %hux", multipressClickCount);

            // Play boop sound for every button press
            playBoop();

            // Reset combination tracking
            waitingForLongPress = false;

            switch (multipressClickCount) {
#if HAS_GPS && !defined(ELECROW_ThinkNode_M1)
            // 3 clicks: toggle GPS
            case 3:
                if (!config.device.disable_triple_click && (gps != nullptr)) {
                    gps->toggleGpsMode();
                }
                break;
#endif

#if defined(USE_EINK) && defined(PIN_EINK_EN) && !defined(ELECROW_ThinkNode_M1) // i.e. T-Echo
            // 4 clicks: toggle backlight
            case 4:
                digitalWrite(PIN_EINK_EN, digitalRead(PIN_EINK_EN) == LOW);
                break;
#endif
            // No valid multipress action
            default:
                break;
            } // end switch: click count

            break;
        } // end multipress event

        case BUTTON_EVENT_LONG_PRESSED: {
            LOG_BUTTON("Long press!");

            // Check if this is part of a short-press + long-press combination
            if (waitingForLongPress && (millis() - shortPressTime) <= BUTTON_COMBO_TIMEOUT_MS) {
                LOG_BUTTON("Combo detected: short-press + long-press!");
                btnEvent = BUTTON_EVENT_COMBO_SHORT_LONG;
                waitingForLongPress = false;
                break;
            }

            // Forward long press to InputBroker (but NOT as DOWN/SELECT, just forward a "button long press" event)
            InputEvent evt = {"button", INPUT_BROKER_SELECT, 0, 0, 0};
            this->notifyObservers(&evt);

            // Reset combination tracking
            waitingForLongPress = false;

            // Lead-up sound already played during button hold
            // Just a simple beep to confirm long press threshold reached
            playBeep();
            break;
        }

        // Do actual shutdown when button released, otherwise the button release
        // may wake the board immediatedly.
        case BUTTON_EVENT_LONG_RELEASED: {
            if (!screen) {
                LOG_INFO("Shutdown from long press");

                // Reset combination tracking
                waitingForLongPress = false;

                playShutdownMelody();
                delay(3000);
                power->shutdown();
                nodeDB->saveToDisk();
            }
            break;
        }

#ifdef BUTTON_PIN_TOUCH
        case BUTTON_EVENT_TOUCH_LONG_PRESSED: {
            LOG_BUTTON("Touch press!");

            // Play boop sound for every button press
            playBoop();

            // Reset combination tracking
            waitingForLongPress = false;

#ifdef TTGO_T_ECHO
            // Ignore if: TX in progress
            // Uncommon T-Echo hardware bug, LoRa TX triggers touch button
            if (!RadioLibInterface::instance || RadioLibInterface::instance->isSending())
                break;
#endif

            break;
        }
#endif // BUTTON_PIN_TOUCH

        case BUTTON_EVENT_COMBO_SHORT_LONG: {
            // Placeholder for short-press + long-press combination
            LOG_BUTTON("Short-press + Long-press combination detected!");

            // Play the combination tune
            playComboTune();

            // Optionally show a message on screen
            if (screen) {
                screen->showOverlayBanner("Combo Tune Played", 2000);
            }
            break;
        }

        default:
            break;
        }
        btnEvent = BUTTON_EVENT_NONE;
    }

    return 50;
}

/*
 * Attach (or re-attach) hardware interrupts for buttons
 * Public method. Used outside class when waking from MCU sleep
 */
void ButtonThread::attachButtonInterrupts()
{
#if defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC)
        wakeOnIrq(settingsMap[user], FALLING);
#elif defined(BUTTON_PIN)
    // Interrupt for user button, during normal use. Improves responsiveness.
    attachInterrupt(
#if !defined(USERPREFS_BUTTON_PIN)
        config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN,
#endif
#if defined(USERPREFS_BUTTON_PIN)
        config.device.button_gpio ? config.device.button_gpio : USERPREFS_BUTTON_PIN,
#endif
        []() {
            ButtonThread::userButton.tick();
            runASAP = true;
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
        },
        CHANGE);
#endif

#ifdef BUTTON_PIN_ALT
#ifdef ELECROW_ThinkNode_M2
    wakeOnIrq(BUTTON_PIN_ALT, RISING);
#else
    wakeOnIrq(BUTTON_PIN_ALT, FALLING);
#endif
#endif

#ifdef BUTTON_PIN_TOUCH
    wakeOnIrq(BUTTON_PIN_TOUCH, FALLING);
#endif
}

/*
 * Detach the "normal" button interrupts.
 * Public method. Used before attaching a "wake-on-button" interrupt for MCU sleep
 */
void ButtonThread::detachButtonInterrupts()
{
#if defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC)
        detachInterrupt(settingsMap[user]);
#elif defined(BUTTON_PIN)
#if !defined(USERPREFS_BUTTON_PIN)
    detachInterrupt(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN);
#endif
#if defined(USERPREFS_BUTTON_PIN)
    detachInterrupt(config.device.button_gpio ? config.device.button_gpio : USERPREFS_BUTTON_PIN);
#endif
#endif

#ifdef BUTTON_PIN_ALT
    detachInterrupt(BUTTON_PIN_ALT);
#endif

#ifdef BUTTON_PIN_TOUCH
    detachInterrupt(BUTTON_PIN_TOUCH);
#endif
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

/**
 * Watch a GPIO and if we get an IRQ, wake the main thread.
 * Use to add wake on button press
 */
void ButtonThread::wakeOnIrq(int irq, int mode)
{
    attachInterrupt(
        irq,
        [] {
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
            runASAP = true;
        },
        FALLING);
}

// Static callback
void ButtonThread::userButtonMultiPressed(void *callerThread)
{
    // Grab click count from non-static button, while the info is still valid
    ButtonThread *thread = (ButtonThread *)callerThread;
    thread->storeClickCount();

    // Then handle later, in the usual way
    btnEvent = BUTTON_EVENT_MULTI_PRESSED;
}

// Non-static method, runs during callback. Grabs info while still valid
void ButtonThread::storeClickCount()
{
#if defined(BUTTON_PIN) || defined(USERPREFS_BUTTON_PIN)
    multipressClickCount = userButton.getNumberClicks();
#endif
}

void ButtonThread::userButtonPressedLongStart()
{
    if (millis() > c_holdOffTime) {
        btnEvent = BUTTON_EVENT_LONG_PRESSED;
    }
}

void ButtonThread::userButtonPressedLongStop()
{
    if (millis() > c_holdOffTime) {
        btnEvent = BUTTON_EVENT_LONG_RELEASED;
    }
}