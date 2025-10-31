#include "ButtonsLEDsAndMsgs.h"
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

ButtonsLEDsAndMsgs::ButtonsLEDsAndMsgs(const char *name) : OSThread(name)
{
    _originName = name;
}

bool ButtonsLEDsAndMsgs::initButton(const ButtonConfigModules &config)
{
    if (inputBroker)
        inputBroker->registerSource(this);
    _longPressTime = config.longPressTime;
    _longLongPressTime = config.longLongPressTime;
    _pinNum = config.pinNumber;
    _activeLow = config.activeLow;
    _touchQuirk = config.touchQuirk;
    _intRoutine = config.intRoutine;
    _longLongPress = config.longLongPress;

    userButton = OneButton(config.pinNumber, config.activeLow, config.activePullup);

    if (config.pullupSense != 0) {
        pinMode(config.pinNumber, config.pullupSense);
    }

    _singlePress = config.singlePress;
    userButton.attachClick(
        [](void *callerThread) -> void {
            ButtonsLEDsAndMsgs *thread = (ButtonsLEDsAndMsgs *)callerThread;
            thread->btnEvent = BUTTON_EVENT_PRESSED;
            LOG_DEBUG("ButtonsLEDsAndMsgs(%s): click detected on pin %u", thread->_originName ? thread->_originName : "(null)",
                      (unsigned)thread->_pinNum);
        },
        this);

    _longPress = config.longPress;
    userButton.attachLongPressStart(
        [](void *callerThread) -> void {
            ButtonsLEDsAndMsgs *thread = (ButtonsLEDsAndMsgs *)callerThread;
            thread->btnEvent = BUTTON_EVENT_LONG_PRESSED;
            LOG_DEBUG("ButtonsLEDsAndMsgs(%s): long press start on pin %u",
                      thread->_originName ? thread->_originName : "(null)", (unsigned)thread->_pinNum);
        },
        this);
    userButton.attachLongPressStop(
        [](void *callerThread) -> void {
            ButtonsLEDsAndMsgs *thread = (ButtonsLEDsAndMsgs *)callerThread;
            thread->btnEvent = BUTTON_EVENT_LONG_RELEASED;
            LOG_DEBUG("ButtonsLEDsAndMsgs(%s): long press stop on pin %u", thread->_originName ? thread->_originName : "(null)",
                      (unsigned)thread->_pinNum);
        },
        this);

    if (config.doublePress != INPUT_BROKER_NONE) {
        _doublePress = config.doublePress;
        userButton.attachDoubleClick(
            [](void *callerThread) -> void {
                ButtonsLEDsAndMsgs *thread = (ButtonsLEDsAndMsgs *)callerThread;
                thread->btnEvent = BUTTON_EVENT_DOUBLE_PRESSED;
                LOG_DEBUG("ButtonsLEDsAndMsgs(%s): double click on pin %u",
                          thread->_originName ? thread->_originName : "(null)", (unsigned)thread->_pinNum);
            },
            this);
    }

    if (config.triplePress != INPUT_BROKER_NONE) {
        _triplePress = config.triplePress;
        userButton.attachMultiClick(
            [](void *callerThread) -> void {
                ButtonsLEDsAndMsgs *thread = (ButtonsLEDsAndMsgs *)callerThread;
                thread->storeClickCount();
                thread->btnEvent = BUTTON_EVENT_MULTI_PRESSED;
                LOG_DEBUG("ButtonsLEDsAndMsgs(%s): multi click (%u) on pin %u",
                          thread->_originName ? thread->_originName : "(null)",
                          (unsigned)thread->multipressClickCount, (unsigned)thread->_pinNum);
            },
            this);
    }
    if (config.shortLong != INPUT_BROKER_NONE) {
        _shortLong = config.shortLong;
    }
#ifdef USE_EINK
    userButton.setDebounceMs(0);
#else
    userButton.setDebounceMs(1);
#endif
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
    LOG_INFO("ButtonsLEDsAndMsgs(%s): initButton pin=%u activeLow=%d activePullup=%d", _originName ? _originName : "(null)",
             (unsigned)config.pinNumber, (int)config.activeLow, (int)config.activePullup);
    return true;
}

int32_t ButtonsLEDsAndMsgs::runOnce()
{
    // If the button is pressed we suppress CPU sleep until release
    canSleep = true; // Assume we should not keep the board awake

    // Check for combination timeout
    if (waitingForLongPress && (millis() - shortPressTime) > BUTTON_COMBO_TIMEOUT_MS) {
        waitingForLongPress = false;
    }

    userButton.tick();
    canSleep &= userButton.isIdle();

    bool buttonCurrentlyPressed = isButtonPressed(_pinNum);

    if (buttonCurrentlyPressed && !buttonWasPressed) {
        buttonPressStartTime = millis();
        leadUpPlayed = false;
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    if (buttonCurrentlyPressed && (millis() - buttonPressStartTime) >= BUTTON_LEADUP_MS) {
        if (!leadUpSequenceActive) {
            leadUpSequenceActive = true;
            lastLeadUpNoteTime = millis();
            playNextLeadUpNote();
        } else if ((millis() - lastLeadUpNoteTime) >= 400) {
            if (playNextLeadUpNote()) {
                lastLeadUpNoteTime = millis();
            } else {
                leadUpPlayed = true;
            }
        }
    }

    if (!buttonCurrentlyPressed && buttonWasPressed) {
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    buttonWasPressed = buttonCurrentlyPressed;

    if (btnEvent != BUTTON_EVENT_NONE) {
            InputEvent evt;
            evt.source = _originName;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
        LOG_INFO("ButtonsLEDsAndMsgs(%s): preparing event %d from pin %u", _originName ? _originName : "(null)", (int)btnEvent,
                 (unsigned)_pinNum);
        switch (btnEvent) {
        case BUTTON_EVENT_PRESSED: {
            evt.inputEvent = _singlePress;
            this->notifyObservers(&evt);

            waitingForLongPress = true;
            shortPressTime = millis();

            break;
        }
        case BUTTON_EVENT_LONG_PRESSED: {
            if (_touchQuirk && RadioLibInterface::instance && RadioLibInterface::instance->isSending())
                break;

            if (_shortLong != INPUT_BROKER_NONE && waitingForLongPress &&
                (millis() - shortPressTime) <= BUTTON_COMBO_TIMEOUT_MS) {
                evt.inputEvent = _shortLong;
                this->notifyObservers(&evt);
                playComboTune();

                break;
            }
            if (_longPress != INPUT_BROKER_NONE) {
                evt.inputEvent = _longPress;
                this->notifyObservers(&evt);
            }
            waitingForLongPress = false;

            break;
        }
        case BUTTON_EVENT_DOUBLE_PRESSED: {
            LOG_INFO("Double press!");
            waitingForLongPress = false;
            evt.inputEvent = _doublePress;
            this->notifyObservers(&evt);
            playComboTune();
            break;
        }
        case BUTTON_EVENT_MULTI_PRESSED: {
            LOG_INFO("Mulitipress! %hux", multipressClickCount);
            waitingForLongPress = false;
            switch (multipressClickCount) {
            case 3:
                evt.inputEvent = _triplePress;
                this->notifyObservers(&evt);
                playComboTune();
                break;
            default:
                break;
            }
            break;
        }
        case BUTTON_EVENT_LONG_RELEASED: {
            LOG_INFO("LONG PRESS RELEASE AFTER %u MILLIS", millis() - buttonPressStartTime);
            if (millis() > 30000 && _longLongPress != INPUT_BROKER_NONE &&
                (millis() - buttonPressStartTime) >= _longLongPressTime && leadUpPlayed) {
                evt.inputEvent = _longLongPress;
                this->notifyObservers(&evt);
            }
            waitingForLongPress = false;
            leadUpPlayed = false;

            break;
        }
        default: {
            break;
        }
        }
    }
    btnEvent = BUTTON_EVENT_NONE;

    if (!userButton.isIdle() || waitingForLongPress) {
        return 50;
    }
    return 100;
}

void ButtonsLEDsAndMsgs::attachButtonInterrupts()
{
    attachInterrupt(_pinNum, _intRoutine, CHANGE);
}

void ButtonsLEDsAndMsgs::detachButtonInterrupts()
{
    detachInterrupt(_pinNum);
}

#ifdef ARCH_ESP32
int ButtonsLEDsAndMsgs::beforeLightSleep(void *unused)
{
    detachButtonInterrupts();
    return 0;
}

int ButtonsLEDsAndMsgs::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachButtonInterrupts();
    return 0;
}
#endif

void ButtonsLEDsAndMsgs::storeClickCount()
{
    multipressClickCount = userButton.getNumberClicks();

}

ButtonsLEDsAndMsgs *buttonsLEDsAndMsgs = nullptr;
