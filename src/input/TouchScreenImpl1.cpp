#include "TouchScreenImpl1.h"
#include "InputBroker.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "modules/ExternalNotificationModule.h"
#include "sleep.h"
#include <cstring>

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#endif

#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

TouchScreenImpl1 *touchScreenImpl1;

// Hardware-interrupt wake on the touch IRQ line. Some touch boards
// either drive this pin differently (RAK14014 already owns this interrupt in TFTDisplay) or
// route it through an IO expander, which can't be used with attachInterrupt().

// use ENABLE_TOUCH_INT to indicate that we should enable the interrupt here.
#if defined(SCREEN_TOUCH_INT) && defined(ENABLE_TOUCH_INT)

// The touch controller pulls SCREEN_TOUCH_INT low when a new touch begins. Wake the polling
// thread immediately so the touch is handled without waiting for the next idle poll. The
// periodic poll in TouchScreenBase::runOnce() remains as a fallback. Mirrors the button
// interrupt handling in ButtonThread/InputBroker.
static void touchScreenInterruptHandler()
{
    if (touchScreenImpl1) {
        touchScreenImpl1->setIntervalFromNow(0);
        runASAP = true;
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
    }
}
#endif

TouchScreenImpl1::TouchScreenImpl1(uint16_t width, uint16_t height, bool (*getTouch)(int16_t *, int16_t *))
    : TouchScreenBase("touchscreen1", width, height), _getTouch(getTouch)
{
}

void TouchScreenImpl1::init()
{
#if ARCH_PORTDUINO
    if (portduino_config.touchscreenModule) {
        TouchScreenBase::init(true);
        inputBroker->registerSource(this);
        attachTouchInterrupt();
    } else {
        TouchScreenBase::init(false);
    }
#elif !HAS_TOUCHSCREEN
    TouchScreenBase::init(false);
    return;
#else
    TouchScreenBase::init(true);
    if (inputBroker)
        inputBroker->registerSource(this);
    attachTouchInterrupt();
#endif

#if defined(ENABLE_TOUCH_INT) && defined(ARCH_ESP32)
    // Detach/reattach our interrupt around light sleep, so sleep.cpp can configure the touch
    // pin as a wake source without our handler interfering.
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
}

// Attach the touch-controller IRQ so a new touch wakes the polling thread immediately.
// No-op on boards without a usable touch interrupt line.
void TouchScreenImpl1::attachTouchInterrupt()
{
#ifdef ENABLE_TOUCH_INT
    pinMode(SCREEN_TOUCH_INT, INPUT_PULLUP);
    attachInterrupt(SCREEN_TOUCH_INT, touchScreenInterruptHandler, FALLING);
    LOG_INFO("TouchScreen interrupt attached on pin %d", SCREEN_TOUCH_INT);
#endif
}

#ifdef ARCH_ESP32
// Detach our interrupt before light sleep; sleep.cpp configures its own wake-on-touch.
int TouchScreenImpl1::beforeLightSleep(void *unused)
{
#ifdef ENABLE_TOUCH_INT
    detachInterrupt(SCREEN_TOUCH_INT);
#endif
    return 0; // Indicates success
}

// Reattach our interrupt after waking from light sleep.
int TouchScreenImpl1::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachTouchInterrupt();
    return 0; // Indicates success
}
#endif

bool TouchScreenImpl1::getTouch(int16_t &x, int16_t &y)
{
    return _getTouch(&x, &y);
}

bool TouchScreenImpl1::fastTapModeEnabled() const
{
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    const auto *inkhud = NicheGraphics::InkHUD::InkHUD::getInstance();
    if (!inkhud) {
        return false;
    }

    for (auto *sa : inkhud->systemApplets) {
        if (!sa || !sa->name) {
            continue;
        }
        if (strcmp(sa->name, "Keyboard") == 0) {
            return sa->isForeground();
        }
    }
#endif
    return false;
}

bool TouchScreenImpl1::longPressEnabled() const
{
    return !fastTapModeEnabled();
}

/**
 * @brief forward touchscreen event
 *
 * @param event
 *
 * The touchscreen events are translated to input events and reversed
 */
void TouchScreenImpl1::onEvent(const TouchEvent &event)
{
    InputEvent e = {};
    e.source = event.source;
    e.kbchar = 0;
    e.touchX = event.x;
    e.touchY = event.y;

    switch (event.touchEvent) {
    case TOUCH_ACTION_LEFT: {
        e.inputEvent = INPUT_BROKER_LEFT;
        break;
    }
    case TOUCH_ACTION_RIGHT: {
        e.inputEvent = INPUT_BROKER_RIGHT;
        break;
    }
    case TOUCH_ACTION_UP: {
        e.inputEvent = INPUT_BROKER_UP;
        break;
    }
    case TOUCH_ACTION_DOWN: {
        e.inputEvent = INPUT_BROKER_DOWN;
        break;
    }
    case TOUCH_ACTION_LONG_PRESS: {
        e.inputEvent = INPUT_BROKER_SELECT;
        break;
    }
    case TOUCH_ACTION_TAP: {
        e.inputEvent = INPUT_BROKER_USER_PRESS;
        break;
    }
    default:
        return;
    }
    this->notifyObservers(&e);
}
