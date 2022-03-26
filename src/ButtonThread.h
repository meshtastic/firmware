#include "configuration.h"
#include "concurrency/OSThread.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "graphics/Screen.h"
#include "power.h"
#include "buzz.h"
#include <OneButton.h>

#ifndef NO_ESP32
#include "nimble/BluetoothUtil.h"
#endif

namespace concurrency
{
/**
 * Watch a GPIO and if we get an IRQ, wake the main thread.
 * Use to add wake on button press
 */
void wakeOnIrq(int irq, int mode)
{
    attachInterrupt(
        irq,
        [] {
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
        },
        FALLING);
}

class ButtonThread : public concurrency::OSThread
{
// Prepare for button presses
#ifdef BUTTON_PIN
    OneButton userButton;
#endif
#ifdef BUTTON_PIN_ALT
    OneButton userButtonAlt;
#endif
#ifdef BUTTON_PIN_TOUCH
    OneButton userButtonTouch;
#endif
    static bool shutdown_on_long_stop;

  public:
    static uint32_t longPressTime;

    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    ButtonThread() : OSThread("Button")
    {
#ifdef BUTTON_PIN
        userButton = OneButton(BUTTON_PIN, true, true);
#ifdef INPUT_PULLUP_SENSE
        // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
        pinMode(BUTTON_PIN, INPUT_PULLUP_SENSE);
#endif
        userButton.attachClick(userButtonPressed);
        userButton.attachDuringLongPress(userButtonPressedLong);
        userButton.attachDoubleClick(userButtonDoublePressed);
        userButton.attachMultiClick(userButtonMultiPressed);
        userButton.attachLongPressStart(userButtonPressedLongStart);
        userButton.attachLongPressStop(userButtonPressedLongStop);
        wakeOnIrq(BUTTON_PIN, FALLING);
#endif
#ifdef BUTTON_PIN_ALT
        userButtonAlt = OneButton(BUTTON_PIN_ALT, true, true);
#ifdef INPUT_PULLUP_SENSE
        // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
        pinMode(BUTTON_PIN_ALT, INPUT_PULLUP_SENSE);
#endif
        userButtonAlt.attachClick(userButtonPressed);
        userButtonAlt.attachDuringLongPress(userButtonPressedLong);
        userButtonAlt.attachDoubleClick(userButtonDoublePressed);
        userButtonAlt.attachLongPressStart(userButtonPressedLongStart);
        userButtonAlt.attachLongPressStop(userButtonPressedLongStop);
        wakeOnIrq(BUTTON_PIN_ALT, FALLING);
#endif

#ifdef BUTTON_PIN_TOUCH
        userButtonTouch = OneButton(BUTTON_PIN_TOUCH, true, true);
#ifdef INPUT_PULLUP_SENSE
        // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
        pinMode(BUTTON_PIN_TOUCH, INPUT_PULLUP_SENSE);
#endif
        userButtonTouch.attachClick(touchPressed);
        userButtonTouch.attachDuringLongPress(touchPressedLong);
        userButtonTouch.attachDoubleClick(touchDoublePressed);
        userButtonTouch.attachLongPressStart(touchPressedLongStart);
        userButtonTouch.attachLongPressStop(touchPressedLongStop);
        wakeOnIrq(BUTTON_PIN_TOUCH, FALLING);
#endif

    }

  protected:
    /// If the button is pressed we suppress CPU sleep until release
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

#ifdef BUTTON_PIN
        userButton.tick();
        canSleep &= userButton.isIdle();
#endif
#ifdef BUTTON_PIN_ALT
        userButtonAlt.tick();
        canSleep &= userButtonAlt.isIdle();
#endif
#ifdef BUTTON_PIN_TOUCH
        userButtonTouch.tick();
        canSleep &= userButtonTouch.isIdle();
#endif
        // if (!canSleep) DEBUG_MSG("Supressing sleep!\n");
        // else DEBUG_MSG("sleep ok\n");

        return 5;
    }

  private:
    static void touchPressed()
    {        
        screen->forceDisplay();
        DEBUG_MSG("touch press!\n");       
    }
    static void touchDoublePressed()
    {
        DEBUG_MSG("touch double press!\n");       
    }
    static void touchPressedLong()
    {
        DEBUG_MSG("touch press long!\n");       
    }
    static void touchDoublePressedLong()
    {
        DEBUG_MSG("touch double pressed!\n");       
    }
    static void touchPressedLongStart()
    {        
        DEBUG_MSG("touch long press start!\n");       
    }
    static void touchPressedLongStop()
    {       
        DEBUG_MSG("touch long press stop!\n");       
    }


    static void userButtonPressed()
    {
        // DEBUG_MSG("press!\n");
#ifdef BUTTON_PIN
        if ((BUTTON_PIN != radioConfig.preferences.inputbroker_pin_press) || !radioConfig.preferences.canned_message_module_enabled) {
            powerFSM.trigger(EVENT_PRESS);
        }
#endif
    }
    static void userButtonPressedLong()
    {
        // DEBUG_MSG("Long press!\n");
#ifndef NRF52_SERIES
        screen->adjustBrightness();
#endif
        // If user button is held down for 5 seconds, shutdown the device.
        if (millis() - longPressTime > 5 * 1000) {
#ifdef TBEAM_V10
            if (axp192_found == true) {
                setLed(false);
                power->shutdown();
            }
#elif NRF52_SERIES
            // Do actual shutdown when button released, otherwise the button release
            // may wake the board immediatedly.
            if (!shutdown_on_long_stop) {
                screen->startShutdownScreen();
                DEBUG_MSG("Shutdown from long press");
                playBeep();
                ledOff(PIN_LED1);
                ledOff(PIN_LED2);
                shutdown_on_long_stop = true;
            }
#endif
        } else {
            // DEBUG_MSG("Long press %u\n", (millis() - longPressTime));
        }
    }

    static void userButtonDoublePressed()
    {
#ifndef NO_ESP32
        disablePin();
#elif defined(HAS_EINK)
        digitalWrite(PIN_EINK_EN,digitalRead(PIN_EINK_EN) == LOW);
#endif
    }

    static void userButtonMultiPressed()
    {
#ifndef NO_ESP32
        clearNVS();
#endif
#ifdef NRF52_SERIES
        clearBonds();
#endif
    }


    static void userButtonPressedLongStart()
    {
        DEBUG_MSG("Long press start!\n");
        longPressTime = millis();
    }

    static void userButtonPressedLongStop()
    {
        DEBUG_MSG("Long press stop!\n");
        longPressTime = 0;
        if (shutdown_on_long_stop) {
            playShutdownMelody();
            delay(3000);
            power->shutdown();
        }
    }
};

}