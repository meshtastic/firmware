#include "ButtonThread.h"
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshService.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "main.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

#define DEBUG_BUTTONS 0
#if DEBUG_BUTTONS
#define LOG_BUTTON(...) LOG_DEBUG(__VA_ARGS__)
#else
#define LOG_BUTTON(...)
#endif

using namespace concurrency;

ButtonThread *buttonThread; // Declared extern in header
volatile ButtonThread::ButtonEventType ButtonThread::btnEvent = ButtonThread::BUTTON_EVENT_NONE;

#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO)
OneButton ButtonThread::userButton; // Get reference to static member
#endif
ButtonThread::ButtonThread() : OSThread("Button")
{
#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO)

#if defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC) {
        this->userButton = OneButton(settingsMap[user], true, true);
        LOG_DEBUG("Using GPIO%02d for button\n", settingsMap[user]);
    }
#elif defined(BUTTON_PIN)
    int pin = config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN; // Resolved button pin
#if defined(HELTEC_CAPSULE_SENSOR_V3)
    this->userButton = OneButton(pin, false, false);
#elif defined(BUTTON_ACTIVE_LOW)
    this->userButton = OneButton(pin, BUTTON_ACTIVE_LOW, BUTTON_ACTIVE_PULLUP);
#else
    this->userButton = OneButton(pin, true, true);
#endif
    LOG_DEBUG("Using GPIO%02d for button\n", pin);
#endif

#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
#ifdef BUTTON_SENSE_TYPE
    pinMode(pin, BUTTON_SENSE_TYPE);
#else
    pinMode(pin, INPUT_PULLUP_SENSE);
#endif
#endif

#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO)
    userButton.attachClick(userButtonPressed);
    userButton.setClickMs(BUTTON_CLICK_MS);
    userButton.setPressMs(BUTTON_LONGPRESS_MS);
    userButton.setDebounceMs(1);
    userButton.attachDoubleClick(userButtonDoublePressed);
    userButton.attachMultiClick(userButtonMultiPressed, this); // Reference to instance: get click count from non-static OneButton
#ifndef T_DECK // T-Deck immediately wakes up after shutdown, so disable this function
    userButton.attachLongPressStart(userButtonPressedLongStart);
    userButton.attachLongPressStop(userButtonPressedLongStop);
#endif
#endif

#ifdef BUTTON_PIN_ALT
    userButtonAlt = OneButton(BUTTON_PIN_ALT, true, true);
#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
    pinMode(BUTTON_PIN_ALT, INPUT_PULLUP_SENSE);
#endif
    userButtonAlt.attachClick(userButtonPressed);
    userButtonAlt.setClickMs(BUTTON_CLICK_MS);
    userButtonAlt.setPressMs(BUTTON_LONGPRESS_MS);
    userButtonAlt.setDebounceMs(1);
    userButtonAlt.attachDoubleClick(userButtonDoublePressed);
    userButtonAlt.attachLongPressStart(userButtonPressedLongStart);
    userButtonAlt.attachLongPressStop(userButtonPressedLongStop);
#endif

#ifdef BUTTON_PIN_TOUCH
    userButtonTouch = OneButton(BUTTON_PIN_TOUCH, true, true);
    userButtonTouch.setPressMs(BUTTON_TOUCH_MS);
    userButtonTouch.attachLongPressStart(touchPressedLongStart); // Better handling with longpress than click?
#endif

    attachButtonInterrupts();
#endif
}

int32_t ButtonThread::runOnce()
{
    // If the button is pressed we suppress CPU sleep until release
    canSleep = true; // Assume we should not keep the board awake

#if defined(BUTTON_PIN)
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

    if (btnEvent != BUTTON_EVENT_NONE) {
        switch (btnEvent) {
        case BUTTON_EVENT_PRESSED: {
            LOG_BUTTON("press!\n");
            // If a nag notification is running, stop it and prevent other actions
            if (moduleConfig.external_notification.enabled && (externalNotificationModule->nagCycleCutoff != UINT32_MAX)) {
                externalNotificationModule->stopNow();
                return 50;
            }
#ifdef BUTTON_PIN
            if (((config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN) !=
                 moduleConfig.canned_message.inputbroker_pin_press) ||
                !(moduleConfig.canned_message.updown1_enabled || moduleConfig.canned_message.rotary1_enabled) ||
                !moduleConfig.canned_message.enabled) {
                powerFSM.trigger(EVENT_PRESS);
            }
#endif
#if defined(ARCH_PORTDUINO)
            if ((settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC) &&
                    (settingsMap[user] != moduleConfig.canned_message.inputbroker_pin_press) ||
                !moduleConfig.canned_message.enabled) {
                powerFSM.trigger(EVENT_PRESS);
            }
#endif
            break;
        }

        case BUTTON_EVENT_DOUBLE_PRESSED: {
            LOG_BUTTON("Double press!\n");
            service->refreshLocalMeshNode();
            auto sentPosition = service->trySendPosition(NODENUM_BROADCAST, true);
            if (screen) {
                if (sentPosition)
                    screen->print("Sent ad-hoc position\n");
                else
                    screen->print("Sent ad-hoc nodeinfo\n");
                screen->forceDisplay(true); // Force a new UI frame, then force an EInk update
            }
            break;
        }

        case BUTTON_EVENT_MULTI_PRESSED: {
            LOG_BUTTON("Mulitipress! %hux\n", multipressClickCount);
            switch (multipressClickCount) {
#if HAS_GPS
            // 3 clicks: toggle GPS
            case 3:
                if (!config.device.disable_triple_click && (gps != nullptr)) {
                    gps->toggleGpsMode();
                    if (screen)
                        screen->forceDisplay(true); // Force a new UI frame, then force an EInk update
                }
                break;
#endif
#if defined(USE_EINK) && defined(PIN_EINK_EN) // i.e. T-Echo
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
            LOG_BUTTON("Long press!\n");
            powerFSM.trigger(EVENT_PRESS);
            if (screen) {
                screen->startAlert("Shutting down...");
            }
            playBeep();
            break;
        }

        // Do actual shutdown when button released, otherwise the button release
        // may wake the board immediatedly.
        case BUTTON_EVENT_LONG_RELEASED: {
            LOG_INFO("Shutdown from long press\n");
            playShutdownMelody();
            delay(3000);
            power->shutdown();
            break;
        }

#ifdef BUTTON_PIN_TOUCH
        case BUTTON_EVENT_TOUCH_LONG_PRESSED: {
            LOG_BUTTON("Touch press!\n");
            if (screen) {
                // Wake if asleep
                if (powerFSM.getState() == &stateDARK)
                    powerFSM.trigger(EVENT_PRESS);

                // Update display (legacy behaviour)
                screen->forceDisplay();
            }
            break;
        }
#endif // BUTTON_PIN_TOUCH

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
        config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN,
        []() {
            ButtonThread::userButton.tick();
            runASAP = true;
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
        },
        CHANGE);
#endif

#ifdef BUTTON_PIN_ALT
    wakeOnIrq(BUTTON_PIN_ALT, FALLING);
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
    detachInterrupt(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN);
#endif

#ifdef BUTTON_PIN_ALT
    detachInterrupt(BUTTON_PIN_ALT);
#endif

#ifdef BUTTON_PIN_TOUCH
    detachInterrupt(BUTTON_PIN_TOUCH);
#endif
}

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
#ifdef BUTTON_PIN
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