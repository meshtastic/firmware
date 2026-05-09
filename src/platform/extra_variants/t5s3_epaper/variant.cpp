#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

#include "Observer.h"
#include "TouchDrvGT911.hpp"
#include "Wire.h"
#include "buzz.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"
#include "main.h"
#include "sleep.h"
#include <cstring>

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

// Bridges touch events from TouchScreenImpl1 directly into InkHUD,
// bypassing the InputBroker (which is excluded in InkHUD builds).
// Routing mirrors the mini-epaper-s3 two-way rocker pattern:
//   - Nav left/right: prevApplet/nextApplet when idle, navUp/Down when a system applet has focus (e.g. menu)
//   - Nav up/down:    navUp/navDown always (menu scroll)
//   - Tap/long-press: direct touch point dispatch (with fallback to short/long button semantics)
class TouchInkHUDBridge : public Observer<const InputEvent *>
{
    int onNotify(const InputEvent *e) override
    {
        auto *inkhud = NicheGraphics::InkHUD::InkHUD::getInstance();

        // Keep alignment in sync with the current rotation so that visual-frame gestures
        // always pass through nav functions without remapping: (rotation + alignment) % 4 == 0.
        inkhud->persistence->settings.joystick.alignment = (4 - inkhud->persistence->settings.rotation) % 4;

        // Check whether a system applet (e.g. menu) is currently handling input
        bool systemHandlingInput = false;
        for (NicheGraphics::InkHUD::SystemApplet *sa : inkhud->systemApplets) {
            if (sa->handleInput) {
                systemHandlingInput = true;
                break;
            }
        }

        switch (e->inputEvent) {
        case INPUT_BROKER_USER_PRESS:
            inkhud->touchTap(e->touchX, e->touchY);
            break;
        case INPUT_BROKER_SELECT:
            inkhud->touchLongPress(e->touchX, e->touchY);
            break;
        case INPUT_BROKER_LEFT:
            if (systemHandlingInput)
                inkhud->touchNavUp();
            else
                inkhud->prevApplet();
            break;
        case INPUT_BROKER_RIGHT:
            if (systemHandlingInput)
                inkhud->touchNavDown();
            else
                inkhud->nextApplet();
            break;
        case INPUT_BROKER_UP:
            inkhud->touchNavUp();
            break;
        case INPUT_BROKER_DOWN:
            inkhud->touchNavDown();
            break;
        default:
            break;
        }
        return 0;
    }
};

static TouchInkHUDBridge touchBridge;
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS

TouchDrvGT911 touch;

namespace
{
constexpr uint8_t BACKLIGHT_ON_LEVEL = HIGH;
constexpr uint8_t BACKLIGHT_OFF_LEVEL = LOW;
volatile bool backlightUserEnabled = true;
volatile bool backlightForcedByTimeout = false;
volatile bool backlightForcedBySleep = false;

void applyBacklightState()
{
    const bool shouldOn = backlightUserEnabled && !backlightForcedByTimeout && !backlightForcedBySleep;
    digitalWrite(BOARD_BL_EN, shouldOn ? BACKLIGHT_ON_LEVEL : BACKLIGHT_OFF_LEVEL);
}

volatile bool touchInputEnabled = true;
volatile bool touchForcedByTimeout = false;
volatile bool touchControllerReady = false;
volatile bool touchLightSleepActive = false;
volatile bool touchNeedsWake = false;
volatile bool touchIndicatorRefreshPending = false;
volatile uint32_t touchResumeBlockUntilMs = 0;
volatile uint32_t touchStateEpoch = 1;
volatile bool homeCapButtonEventsEnabled = false;
#if HAS_SCREEN
uint32_t lastTouchIndicatorMs = 0;
#endif

void showTouchIndicator(const char *text)
{
#if HAS_SCREEN
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    // InkHUD builds render a dedicated bottom-edge "TOUCH OFF" overlay instead of popup banners.
    (void)text;
    return;
#else
    // Keep repeated notifications low profile and non-spammy.
    if ((millis() - lastTouchIndicatorMs) < 500) {
        return;
    }
    lastTouchIndicatorMs = millis();
    if (screen) {
        screen->showSimpleBanner(text, 1400);
    }
#endif
#else
    (void)text;
#endif
}

#if defined(BOARD_PCA9535_ADDR) && defined(BOARD_PCA9535_BUTTON_MASK)
bool readPca9535Port1(uint8_t *value)
{
    if (!value) {
        return false;
    }

    Wire.beginTransmission(BOARD_PCA9535_ADDR);
    Wire.write((uint8_t)0x01); // input port 1
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)BOARD_PCA9535_ADDR, (uint8_t)1) != 1) {
        return false;
    }

    *value = Wire.read();
    return true;
}

bool isPca9535SideKeyPressed()
{
    uint8_t port1 = 0xFF;
    if (!readPca9535Port1(&port1)) {
        return false;
    }

    return (port1 & BOARD_PCA9535_BUTTON_MASK) == 0;
}

class SideKeyInterruptThread : public concurrency::OSThread
{
  public:
    SideKeyInterruptThread() : concurrency::OSThread("t5s3SideKeyInt", SAMPLE_MS)
    {
        // Do not run unless an edge arrives.
        OSThread::disable();
        instance = this;
#ifdef ARCH_ESP32
        lsObserver.observe(&notifyLightSleep);
        lsEndObserver.observe(&notifyLightSleepEnd);
#endif
    }

    void begin()
    {
        pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
        attachInterrupt(BOARD_PCA9535_INT, SideKeyInterruptThread::isr, FALLING);
    }

  protected:
    int32_t runOnce() override
    {
        const uint32_t now = millis();

        if (now < touchResumeBlockUntilMs) {
            resetStateAndStop();
            return OSThread::disable();
        }

        if (touchLightSleepActive) {
            resetStateAndStop();
            return OSThread::disable();
        }

        // Ignore side-key handling while BOOT/user button is held.
        if (digitalRead(BUTTON_PIN) == LOW) {
            resetStateAndStop();
            return OSThread::disable();
        }

        switch (state) {
        case State::IRQ_PENDING:
            // Initial debounce after expander interrupt edge.
            if ((uint32_t)(now - irqAtMs) < DEBOUNCE_MS) {
                return SAMPLE_MS;
            }

            if (isPca9535SideKeyPressed()) {
                state = State::PRESSED;
                pressStartMs = now;
                return SAMPLE_MS;
            }

            // Spurious/cleared edge.
            resetStateAndStop();
            return OSThread::disable();

        case State::PRESSED: {
            if (isPca9535SideKeyPressed()) {
                // Fire long-press action as soon as threshold is reached, without waiting for release.
                if (!longPressFired && (uint32_t)(now - pressStartMs) >= LONG_PRESS_MIN_MS &&
                    (uint32_t)(now - lastActionMs) >= ACTION_COOLDOWN_MS) {
                    t5BacklightToggleUser();
                    longPressFired = true;
                    lastActionMs = now;
                }
                return SAMPLE_MS;
            }

            // Released: if long-press already fired, do nothing. Otherwise classify short press.
            const uint32_t heldMs = now - pressStartMs;
            if (!longPressFired && heldMs >= SHORT_PRESS_MIN_MS && (uint32_t)(now - lastActionMs) >= ACTION_COOLDOWN_MS) {
                // If timeout forced touch/backlight off, short-press acts as a wake action first.
                if (t5TouchIsForcedByTimeout()) {
                    t5TouchHandleUserInput();
                    t5BacklightHandleUserInput();
                } else {
                    toggleTouchInputEnabled();
                }
                lastActionMs = now;
            }

            resetStateAndStop();
            return OSThread::disable();
        }

        case State::REST:
        default:
            return OSThread::disable();
        }
    }

  private:
    enum class State : uint8_t {
        REST,
        IRQ_PENDING,
        PRESSED,
    };

    static constexpr uint32_t SAMPLE_MS = 15;
    static constexpr uint32_t DEBOUNCE_MS = 25;
    static constexpr uint32_t SHORT_PRESS_MIN_MS = 30;
    static constexpr uint32_t LONG_PRESS_MIN_MS = 450;
    static constexpr uint32_t ACTION_COOLDOWN_MS = 180;

    static SideKeyInterruptThread *instance;

    static void isr()
    {
        if (instance) {
            instance->onInterruptEdge();
        }
    }

    void onInterruptEdge()
    {
        if (touchLightSleepActive) {
            return;
        }
        const uint32_t now = millis();
        if (now < touchResumeBlockUntilMs) {
            return;
        }
        if (state != State::REST) {
            return;
        }

        state = State::IRQ_PENDING;
        irqAtMs = millis();
        startThread();
    }

    void startThread()
    {
        if (!OSThread::enabled) {
            OSThread::setIntervalFromNow(0);
            OSThread::enabled = true;
            runASAP = true;
        }
    }

    void resetStateAndStop()
    {
        state = State::REST;
        longPressFired = false;
        if (OSThread::enabled) {
            OSThread::disable();
        }
    }

#ifdef ARCH_ESP32
    int onLightSleep(void *)
    {
        detachInterrupt(BOARD_PCA9535_INT);
        // Clear any latched PCA9535 interrupt before enabling GPIO wake.
        // If INT is left asserted low, light sleep exits immediately.
        uint8_t ignored = 0xFF;
        (void)readPca9535Port1(&ignored);
        resetStateAndStop();
        return 0;
    }

    int onLightSleepEnd(esp_sleep_wakeup_cause_t cause)
    {
        (void)cause;
        // Consume any pending interrupt source before reattaching ISR.
        uint8_t ignored = 0xFF;
        (void)readPca9535Port1(&ignored);
        pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
        attachInterrupt(BOARD_PCA9535_INT, SideKeyInterruptThread::isr, FALLING);

        return 0;
    }

    CallbackObserver<SideKeyInterruptThread, void *> lsObserver{this, &SideKeyInterruptThread::onLightSleep};
    CallbackObserver<SideKeyInterruptThread, esp_sleep_wakeup_cause_t> lsEndObserver{this,
                                                                                     &SideKeyInterruptThread::onLightSleepEnd};
#endif

    volatile State state = State::REST;
    volatile uint32_t irqAtMs = 0;
    uint32_t pressStartMs = 0;
    bool longPressFired = false;
    uint32_t lastActionMs = 0;
};

SideKeyInterruptThread *SideKeyInterruptThread::instance = nullptr;
SideKeyInterruptThread *sideKeyThread = nullptr;
#endif

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
void refreshTouchIndicatorInInkHUD(bool async = true)
{
    auto *inkhud = NicheGraphics::InkHUD::InkHUD::getInstance();
    NicheGraphics::InkHUD::SystemApplet *touchStatus = nullptr;
    for (auto *sa : inkhud->systemApplets) {
        if (sa && sa->name && strcmp(sa->name, "TouchStatus") == 0) {
            touchStatus = sa;
            break;
        }
    }

    if (touchStatus) {
        if (inkhud->isTouchEnabled())
            touchStatus->sendToBackground();
        else
            touchStatus->bringToForeground();
    }

    // Re-render all applets so touch-status visibility changes are immediately reflected.
    inkhud->forceUpdate(NicheGraphics::Drivers::EInk::UpdateTypes::FAST, true, async);
}
#endif

} // namespace

void t5BacklightSetUserEnabled(bool enabled)
{
    backlightUserEnabled = enabled;
    if (enabled) {
        // Manual ON should release auto-off gates.
        backlightForcedByTimeout = false;
        backlightForcedBySleep = false;
    }
    applyBacklightState();
}

bool t5BacklightIsUserEnabled()
{
    return backlightUserEnabled;
}

void t5BacklightToggleUser()
{
    t5BacklightSetUserEnabled(!backlightUserEnabled);
}

void t5BacklightSetForcedByTimeout(bool forced)
{
    backlightForcedByTimeout = forced;
    applyBacklightState();
}

void t5BacklightSetForcedBySleep(bool forced)
{
    backlightForcedBySleep = forced;
    applyBacklightState();
}

void t5BacklightHandleUserInput()
{
    // Screen-timeout should be lifted by direct user interaction.
    backlightForcedByTimeout = false;
    applyBacklightState();
}

void t5TouchSetForcedByTimeout(bool forced)
{
    touchForcedByTimeout = forced;
    touchStateEpoch++;
    touchIndicatorRefreshPending = true;

    if (forced) {
        // While timeout-forced, keep controller asleep to avoid stale IRQ chatter.
        touchNeedsWake = false;
        if (touchControllerReady && !touchLightSleepActive) {
            touch.sleep();
        }
    } else if (touchInputEnabled && touchControllerReady && !touchLightSleepActive) {
        // Defer wake until readTouch() so I2C settles post-state transition.
        touchNeedsWake = true;
    }

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    if (!touchLightSleepActive) {
        refreshTouchIndicatorInInkHUD();
        touchIndicatorRefreshPending = false;
    }
#endif
}

bool t5TouchIsForcedByTimeout()
{
    return touchForcedByTimeout;
}

void t5TouchHandleUserInput()
{
    t5TouchSetForcedByTimeout(false);
}

void t5SetHomeCapButtonEventsEnabled(bool enabled)
{
    homeCapButtonEventsEnabled = enabled;
}

bool isTouchInputEnabled()
{
    return touchInputEnabled && !touchForcedByTimeout && !touchLightSleepActive;
}

void setTouchInputEnabled(bool enabled, bool showIndicator)
{
    if (touchInputEnabled == enabled) {
        LOG_DEBUG("touchscreen1: setTouchInputEnabled no-op en=%d", enabled);
        return;
    }

    LOG_DEBUG("touchscreen1: setTouchInputEnabled %d -> %d (showIndicator=%d)", touchInputEnabled, enabled, showIndicator);
    touchInputEnabled = enabled;
    touchStateEpoch++;

    if (enabled) {
        touchNeedsWake = touchControllerReady;
        if (touchControllerReady && !touchLightSleepActive) {
            LOG_DEBUG("touchscreen1: wakeup() on enable");
            touch.wakeup();
            touchNeedsWake = false;
        }
    } else {
        touchNeedsWake = false;
        if (touchControllerReady && !touchLightSleepActive) {
            LOG_DEBUG("touchscreen1: sleep() on disable");
            touch.sleep();
        }
        if (showIndicator) {
            showTouchIndicator("Touch OFF");
            touchIndicatorRefreshPending = true;
        }
    }

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    if (showIndicator && !touchLightSleepActive) {
        refreshTouchIndicatorInInkHUD();
        touchIndicatorRefreshPending = false;
    }
#endif
}

void toggleTouchInputEnabled()
{
    setTouchInputEnabled(!touchInputEnabled, true);
}

// Commands the GT911 into standby before the Wire bus is torn down.
// notifyDeepSleep fires before Wire.end() in doDeepSleep(), so I2C is still available here.
struct TouchDeepSleepObserver {
    int onDeepSleep(void *)
    {
        touch.sleep();
        return 0;
    }
    CallbackObserver<TouchDeepSleepObserver, void *> observer{this, &TouchDeepSleepObserver::onDeepSleep};
} static touchDeepSleepObserver;

#ifdef ARCH_ESP32
struct TouchLightSleepObserver {
    int onLightSleep(void *)
    {
        touchLightSleepActive = true;
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
        // Render touch-off overlay before sleeping so user sees touch is unavailable.
        touchIndicatorRefreshPending = true;
        refreshTouchIndicatorInInkHUD(false);
        touchIndicatorRefreshPending = false;
#endif
        return 0;
    }

    CallbackObserver<TouchLightSleepObserver, void *> observer{this, &TouchLightSleepObserver::onLightSleep};
} static touchLightSleepObserver;

struct TouchLightSleepEndObserver {
    int onLightSleepEnd(esp_sleep_wakeup_cause_t cause)
    {
        (void)cause;
        touchLightSleepActive = false;

        if (!touchControllerReady) {
            return 0;
        }

        if (touchInputEnabled && !touchForcedByTimeout) {
            touchNeedsWake = true;
        } else {
            touchNeedsWake = false;
        }

        touchStateEpoch++;
        touchResumeBlockUntilMs = millis() + 150;
        touchIndicatorRefreshPending = !isTouchInputEnabled();
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
        // Clear sleep-time touch overlay after wake.
        touchIndicatorRefreshPending = true;
        refreshTouchIndicatorInInkHUD();
        touchIndicatorRefreshPending = false;
#endif
        return 0;
    }

    CallbackObserver<TouchLightSleepEndObserver, esp_sleep_wakeup_cause_t> observer{this,
                                                                                    &TouchLightSleepEndObserver::onLightSleepEnd};
} static touchLightSleepEndObserver;
#endif

bool readTouch(int16_t *x, int16_t *y)
{
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    static uint32_t suppressUntilMs = 0;
    static uint32_t seenTouchStateEpoch = 0;

    // Reset transient gesture helpers whenever touch mode changes.
    if (seenTouchStateEpoch != touchStateEpoch) {
        seenTouchStateEpoch = touchStateEpoch;
        suppressUntilMs = 0;
    }

    // Let buses and peripherals settle briefly after light-sleep wake.
    if (millis() < touchResumeBlockUntilMs) {
        return false;
    }

    if (touchIndicatorRefreshPending) {
        refreshTouchIndicatorInInkHUD();
        touchIndicatorRefreshPending = false;
    }

    if (!isTouchInputEnabled()) {
        return false;
    }

    if (touchNeedsWake && touchControllerReady) {
        LOG_DEBUG("touchscreen1: wakeup() on deferred resume");
        touch.wakeup();
        touchNeedsWake = false;
        suppressUntilMs = millis() + 60;
        return false;
    }

    // After a recovery pulse, emit a brief "released" window so gesture state can reset.
    if (suppressUntilMs != 0 && millis() < suppressUntilMs) {
        return false;
    }
#endif

    if (!digitalRead(GT911_PIN_INT)) {
        int16_t raw_x;
        int16_t raw_y;
        if (touch.getPoint(&raw_x, &raw_y)) {
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
            // Transform raw GT911 axes to visual-frame coordinates for the current display rotation.
            // rotation=3 is the physical identity (device's default orientation).
            switch (NicheGraphics::InkHUD::InkHUD::getInstance()->persistence->settings.rotation) {
            default:
            case 3:
                *x = raw_x;
                *y = raw_y;
                break; // identity
            case 2:
                *x = (EPD_WIDTH - 1) - raw_y;
                *y = raw_x;
                break; // 90° CW tilt
            case 1:
                *x = (EPD_HEIGHT - 1) - raw_x;
                *y = (EPD_WIDTH - 1) - raw_y;
                break; // 180° flip
            case 0:
                *x = raw_y;
                *y = (EPD_HEIGHT - 1) - raw_x;
                break; // 90° CCW tilt
            }
#else
            *x = raw_x;
            *y = raw_y;
#endif
            LOG_DEBUG("touched(%d/%d)", *x, *y);
            return true;
        }
    }

    return false;
}

void variant_shutdown()
{
    // Ensure backlight is off during deep sleep.
    t5BacklightSetForcedBySleep(true);
}

void lateInitVariant()
{
    touch.setPins(GT911_PIN_RST, GT911_PIN_INT);
    if (touch.begin(Wire, GT911_SLAVE_ADDRESS_H, GT911_PIN_SDA, GT911_PIN_SCL)) {
        // Match LilyGO sample behavior: GT911 center/home capacitive key callback.
        touch.setHomeButtonCallback(
            [](void *user_data) {
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
                if (!homeCapButtonEventsEnabled) {
                    return;
                }

                static uint32_t lastHomeMs = 0;
                const uint32_t now = millis();
                if ((uint32_t)(now - lastHomeMs) < 220) {
                    return; // debounce repeated key reports while still touched
                }
                lastHomeMs = now;

                auto *inkhud = NicheGraphics::InkHUD::InkHUD::getInstance();
                if (inkhud) {
                    // Route through InkHUD EXIT/HOME path (menu close, etc).
                    inkhud->exitShort();
                }
#else
                (void)user_data;
#endif
            },
            nullptr);
        touchControllerReady = true;
        touchInputEnabled = true;
        touchForcedByTimeout = false;
        touchLightSleepActive = false;
        touchStateEpoch++;
        touchDeepSleepObserver.observer.observe(&notifyDeepSleep);
#ifdef ARCH_ESP32
        touchLightSleepObserver.observer.observe(&notifyLightSleep);
        touchLightSleepEndObserver.observer.observe(&notifyLightSleepEnd);
#endif
        touchScreenImpl1 = new TouchScreenImpl1(EPD_WIDTH, EPD_HEIGHT, readTouch);
        touchScreenImpl1->init();
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
        touchBridge.observe(touchScreenImpl1);
#endif
    } else {
        touchControllerReady = false;
        LOG_ERROR("Failed to find touch controller!");
    }

#if defined(BOARD_PCA9535_ADDR) && defined(BOARD_PCA9535_BUTTON_MASK)
    // Start side-key interrupt handling after touch init is complete.
    if (!sideKeyThread) {
        sideKeyThread = new SideKeyInterruptThread();
        sideKeyThread->begin();
    }
#endif
}
#endif
