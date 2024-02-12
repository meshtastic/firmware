#include "ButtonThread.h"
#include "GPS.h"
#include "MeshService.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "graphics/Screen.h"
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

volatile ButtonThread::ButtonEventType ButtonThread::btnEvent = ButtonThread::BUTTON_EVENT_NONE;

ButtonThread::ButtonThread() : OSThread("Button")
{
#if defined(ARCH_PORTDUINO) || defined(BUTTON_PIN)
#if defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC) {
        userButton = OneButton(settingsMap[user], true, true);
        LOG_DEBUG("Using GPIO%02d for button\n", settingsMap[user]);
    }
#elif defined(BUTTON_PIN)
    int pin = config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN;
    this->userButton = OneButton(pin, true, true);
    LOG_DEBUG("Using GPIO%02d for button\n", pin);
#endif

#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
    pinMode(pin, INPUT_PULLUP_SENSE);
#endif
    userButton.attachClick(userButtonPressed);
    userButton.setClickMs(250);
    userButton.setPressMs(c_longPressTime);
    userButton.setDebounceMs(1);
    userButton.attachDoubleClick(userButtonDoublePressed);
    userButton.attachMultiClick(userButtonMultiPressed);
#ifndef T_DECK // T-Deck immediately wakes up after shutdown, so disable this function
    userButton.attachLongPressStart(userButtonPressedLongStart);
    userButton.attachLongPressStop(userButtonPressedLongStop);
#endif
#if defined(ARCH_PORTDUINO)
    if (settingsMap.count(user) != 0 && settingsMap[user] != RADIOLIB_NC)
        wakeOnIrq(settingsMap[user], FALLING);
#else
    static OneButton *pBtn = &userButton; // only one instance of ButtonThread is created, so static is safe
    attachInterrupt(
        pin,
        []() {
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
            pBtn->tick();
        },
        CHANGE);
#endif
#endif
#ifdef BUTTON_PIN_ALT
    userButtonAlt = OneButton(BUTTON_PIN_ALT, true, true);
#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
    pinMode(BUTTON_PIN_ALT, INPUT_PULLUP_SENSE);
#endif
    userButtonAlt.attachClick(userButtonPressed);
    userButtonAlt.setClickMs(250);
    userButtonAlt.setPressMs(c_longPressTime);
    userButtonAlt.setDebounceMs(1);
    userButtonAlt.attachDoubleClick(userButtonDoublePressed);
    userButtonAlt.attachLongPressStart(userButtonPressedLongStart);
    userButtonAlt.attachLongPressStop(userButtonPressedLongStop);
    wakeOnIrq(BUTTON_PIN_ALT, FALLING);
#endif

#ifdef BUTTON_PIN_TOUCH
    userButtonTouch = OneButton(BUTTON_PIN_TOUCH, true, true);
    userButtonTouch.attachClick(touchPressed);
    wakeOnIrq(BUTTON_PIN_TOUCH, FALLING);
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
#if defined(USE_EINK) && defined(PIN_EINK_EN)
            digitalWrite(PIN_EINK_EN, digitalRead(PIN_EINK_EN) == LOW);
#endif
            service.refreshLocalMeshNode();
            service.sendNetworkPing(NODENUM_BROADCAST, true);
            if (screen)
                screen->print("Sent ad-hoc ping\n");
            break;
        }

        case BUTTON_EVENT_MULTI_PRESSED: {
            LOG_BUTTON("Multi press!\n");
            if (!config.device.disable_triple_click && (gps != nullptr)) {
                gps->toggleGpsMode();
                if (screen)
                    screen->forceDisplay();
            }
            break;
        }

        case BUTTON_EVENT_LONG_PRESSED: {
            LOG_BUTTON("Long press!\n");
            powerFSM.trigger(EVENT_PRESS);
            if (screen)
                screen->startShutdownScreen();
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
        case BUTTON_EVENT_TOUCH_PRESSED: {
            LOG_BUTTON("Touch press!\n");
            if (screen)
                screen->forceDisplay();
            break;
        }
        default:
            break;
        }
        btnEvent = BUTTON_EVENT_NONE;
    }

    return 50;
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
        },
        FALLING);
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
