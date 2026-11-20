#ifdef SEEED_MESHPAGER_X2

#include "UptimeClock.h"
#include "buzz/buzz.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "input/InputBroker.h"
#include "main.h"
#include "mesh/Throttle.h"
#ifdef X2_POWER_KEY_UI
#include "PowerFSM.h"
#include "graphics/DeviceScreen.h"
#include "graphics/Screen.h"
#include "sleep.h"
extern DeviceScreen *deviceScreen;
#endif

namespace
{

// The color UI creates no InputBroker, so no ButtonThread watches BUTTON_PIN for a long press.
class PowerButtonThread : public concurrency::OSThread
{
  public:
    PowerButtonThread() : OSThread("PowerButton")
    {
#ifdef X2_POWER_KEY_UI
        preflightObserver.observe(&preflightSleep);
        wakeObserver.observe(&notifyLightSleepEnd);
#endif
    }

  protected:
    int32_t runOnce() override
    {
        static constexpr uint32_t LONG_PRESS_MS = 3000;
        const bool pressed = digitalRead(BUTTON_PIN) == HIGH;

        // Ignore the press that powered the device on until the button has been seen released once.
        if (!armed) {
            armed = !pressed;
            return 100;
        }

#ifdef X2_POWER_KEY_UI
        if (pressed != pendingPressed) {
            pendingPressed = pressed;
            changedAtMs = Time::getMillis();
        }
        if (!Throttle::hasElapsed(changedAtMs, 30) || pressed == stablePressed) {
            playLeadUp(stablePressed);
            return 10;
        }
        stablePressed = pressed;
#endif
        if (pressed && !pressedSinceMs) {
            pressedSinceMs = Time::stampMillis();
            leadUpActive = false;
            resetLeadUpSequence();
        } else if (pressed) {
            playLeadUp(true);
        } else if (pressedSinceMs) {
            leadUpActive = false;
            resetLeadUpSequence();
            // Act on release: with ext1 ANY_HIGH a still-held button would wake the device at once.
            if (Throttle::hasElapsed(pressedSinceMs, LONG_PRESS_MS)) {
                LOG_INFO("Power button long press, shutting down");
                shutdownAtMsec = Time::stampMillis();
            }
#ifdef X2_POWER_KEY_UI
            else if (!wakePress) {
                if (deviceScreen) {
                    deviceScreen->toggleDisplay();
                } else if (screen) {
                    const bool waking = !screen->isScreenOn();
                    screen->setOn(waking);
                    if (waking)
                        powerFSM.trigger(EVENT_INPUT);
                }
            }
            wakePress = false;
#endif
            pressedSinceMs = 0;
        }
#ifdef X2_POWER_KEY_UI
        return 10;
#else
        return 100;
#endif
    }

  private:
    static constexpr uint32_t LEADUP_MS = 2200;

    // Same rising lead-up as ButtonThread so the long press is audible before release.
    void playLeadUp(bool held)
    {
        if (!held || !pressedSinceMs || !Throttle::hasElapsed(pressedSinceMs, LEADUP_MS))
            return;
        if (!leadUpActive) {
            leadUpActive = true;
            lastLeadUpNoteMs = Time::stampMillis();
            playNextLeadUpNote();
        } else if (Throttle::hasElapsed(lastLeadUpNoteMs, 400) && playNextLeadUpNote()) {
            lastLeadUpNoteMs = Time::stampMillis();
        }
    }

    bool armed = false;
    bool leadUpActive = false;
    uint32_t pressedSinceMs = 0;
    uint32_t lastLeadUpNoteMs = 0;
#ifdef X2_POWER_KEY_UI
    int preflight(void *deepSleep) { return !deepSleep && digitalRead(BUTTON_PIN) == HIGH ? 1 : 0; }

    int afterSleep(esp_sleep_wakeup_cause_t)
    {
        if (digitalRead(BUTTON_PIN) == HIGH) {
            wakePress = true;
            stablePressed = pendingPressed = true;
            pressedSinceMs = Time::stampMillis();
            if (deviceScreen)
                deviceScreen->forceWakeup();
            else if (screen)
                screen->setOn(true);
            powerFSM.trigger(EVENT_INPUT);
        }
        return 0;
    }

    CallbackObserver<PowerButtonThread, void *> preflightObserver{this, &PowerButtonThread::preflight};
    CallbackObserver<PowerButtonThread, esp_sleep_wakeup_cause_t> wakeObserver{this, &PowerButtonThread::afterSleep};
    bool pendingPressed = false;
    bool stablePressed = false;
    bool wakePress = false;
    uint32_t changedAtMs = 0;
#endif
};

} // namespace

#ifdef X2_POWER_KEY_UI
bool x2BaseScreenOn()
{
    return !screen || screen->isScreenOn();
}
#endif

void startPowerButtonMonitor()
{
#if !MESHTASTIC_EXCLUDE_INPUTBROKER && !defined(X2_POWER_KEY_UI)
    if (inputBroker)
        return;
#endif
    new PowerButtonThread();
}

#endif