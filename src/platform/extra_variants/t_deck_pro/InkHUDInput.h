#pragma once

#include "configuration.h"

#if defined(T_DECK_PRO) && defined(MESHTASTIC_INCLUDE_INKHUD)

#include "PowerFSM.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "input/TouchScreenImpl1.h"
#include "input/kbI2cBase.h"
#include "modules/SystemCommandsModule.h"
#include "sleep.h"
#include <cstring>

void attachTDeckProTouchInterrupt();

class TDeckProInkHUDInput : public Observer<const InputEvent *>
{
  public:
    void begin()
    {
        observe(touchScreenImpl1);
        observe(&keyboard);
        sleepObserver.observe(&notifyDeepSleep);
        lightSleepObserver.observe(&notifyLightSleep);
        wakeObserver.observe(&notifyLightSleepEnd);
    }

  protected:
    int onNotify(const InputEvent *event) override
    {
        auto *hud = NicheGraphics::InkHUD::InkHUD::getInstance();
        auto &settings = hud->persistence->settings;
        const bool touch = event->source && strcmp(event->source, "touchscreen1") == 0;
        const bool wakeOnly = !touch && hud->isTouchTimedOut();
        powerFSM.trigger(EVENT_INPUT);
        if (wakeOnly)
            return 0;
        settings.joystick.alignment = (4 - settings.rotation) % 4;
        bool typing = false;
        bool systemHandlingInput = false;
        for (auto *applet : hud->systemApplets) {
            typing |= applet->handleFreeText;
            systemHandlingInput |= applet->handleInput;
        }

        auto input = event->inputEvent;
        const input_broker_event directions[] = {INPUT_BROKER_UP, INPUT_BROKER_RIGHT, INPUT_BROKER_DOWN, INPUT_BROKER_LEFT};
        for (uint8_t i = 0; touch && i < 4; ++i) {
            if (input == directions[i]) {
                input = directions[(i + 4 - settings.rotation % 4) % 4];
                break;
            }
        }

        switch (input) {
        case INPUT_BROKER_ALT_PRESS:
            tDeckProToggleBacklights();
            break;
        case INPUT_BROKER_USER_PRESS:
        case INPUT_BROKER_SELECT:
            if (touch) {
                uint16_t x = event->touchX;
                uint16_t y = event->touchY;
                if (x >= EINK_WIDTH || y >= EINK_HEIGHT)
                    return 0;
                rotateTouch(x, y, settings.rotation);
                if (event->inputEvent == INPUT_BROKER_USER_PRESS)
                    hud->touchTap(x, y);
                else
                    hud->touchLongPress(x, y);
            } else if (typing)
                hud->freeTextDone();
            else
                hud->longpress();
            break;
        case INPUT_BROKER_BACK:
            if (typing)
                hud->freeText('\b');
            else
                hud->exitShort();
            break;
        case INPUT_BROKER_CANCEL:
            if (typing)
                hud->freeTextCancel();
            else
                hud->exitShort();
            break;
        case INPUT_BROKER_UP:
            hud->touchNavUp();
            break;
        case INPUT_BROKER_DOWN:
            hud->touchNavDown();
            break;
        case INPUT_BROKER_LEFT:
            if (systemHandlingInput)
                hud->touchNavUp();
            else
                hud->prevApplet();
            break;
        case INPUT_BROKER_RIGHT:
            if (systemHandlingInput)
                hud->touchNavDown();
            else
                hud->nextApplet();
            break;
        case INPUT_BROKER_ANYKEY:
            if (event->kbchar == INPUT_BROKER_MSG_TAB)
                hud->openAppSwitcher();
            else if (event->kbchar == INPUT_BROKER_GPS_TOGGLE || event->kbchar == INPUT_BROKER_SEND_PING) {
                InputEvent command = *event;
                command.inputEvent = static_cast<input_broker_event>(event->kbchar);
                systemCommands.handleInputEvent(&command);
            } else if (event->kbchar == INPUT_BROKER_MSG_MUTE_TOGGLE)
                systemCommands.handleInputEvent(event);
            else if (typing && event->kbchar >= 0x20 && event->kbchar < 0x7F)
                hud->freeText(event->kbchar);
            break;
        default:
            break;
        }
        return 0;
    }

  private:
    SystemCommandsModule systemCommands;
    KbI2cBase keyboard{"TDeckProKeyboard"};
    CallbackObserver<TDeckProInkHUDInput, void *> sleepObserver{this, &TDeckProInkHUDInput::beforeSleep};
    CallbackObserver<TDeckProInkHUDInput, void *> lightSleepObserver{this, &TDeckProInkHUDInput::beforeLightSleep};
    CallbackObserver<TDeckProInkHUDInput, esp_sleep_wakeup_cause_t> wakeObserver{this, &TDeckProInkHUDInput::afterLightSleep};

    int beforeLightSleep(void *)
    {
        tDeckProUpdateBacklights(true);
        detachInterrupt(CST328_PIN_INT);
        return 0;
    }

    int afterLightSleep(esp_sleep_wakeup_cause_t)
    {
        tDeckProUpdateBacklights();
        attachTDeckProTouchInterrupt();
        return 0;
    }

    int beforeSleep(void *)
    {
        tDeckProUpdateBacklights(true);
        digitalWrite(PIN_VIBRATION, LOW);
        return 0;
    }

    static void rotateTouch(uint16_t &x, uint16_t &y, uint8_t rotation)
    {
        const uint16_t rawX = x;
        const uint16_t rawY = y;
        switch (rotation % 4) {
        case 1:
            x = rawY;
            y = EINK_WIDTH - 1 - rawX;
            break;
        case 2:
            x = EINK_WIDTH - 1 - rawX;
            y = EINK_HEIGHT - 1 - rawY;
            break;
        case 3:
            x = EINK_HEIGHT - 1 - rawY;
            y = rawX;
            break;
        }
    }
};

#endif
