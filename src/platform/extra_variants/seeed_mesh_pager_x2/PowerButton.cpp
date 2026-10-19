#ifdef SEEED_MESHPAGER_X2

#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "input/InputBroker.h"
#include "main.h"
#include "mesh/Throttle.h"

namespace
{

// The color UI creates no InputBroker, so no ButtonThread watches BUTTON_PIN for a long press.
class PowerButtonThread : public concurrency::OSThread
{
  public:
    PowerButtonThread() : OSThread("PowerButton") {}

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

        if (pressed && !pressedSinceMs) {
            pressedSinceMs = Time::stampMillis();
        } else if (!pressed && pressedSinceMs) {
            // Act on release: with ext1 ANY_HIGH a still-held button would wake the device at once.
            if (Throttle::hasElapsed(pressedSinceMs, LONG_PRESS_MS)) {
                LOG_INFO("Power button long press, shutting down");
                shutdownAtMsec = Time::stampMillis();
            }
            pressedSinceMs = 0;
        }
        return 100;
    }

  private:
    bool armed = false;
    uint32_t pressedSinceMs = 0;
};

} // namespace

void startPowerButtonMonitor()
{
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    if (inputBroker)
        return;
#endif
    new PowerButtonThread();
}

#endif