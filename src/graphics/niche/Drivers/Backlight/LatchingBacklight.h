/*

    Singleton class
    On-demand control of a display's backlight, connected to a GPIO
    Initial use case is control of T-Echo's frontlight, via the capacitive touch button

    - momentary on
    - latched on

*/

#pragma once

#include "configuration.h"

#include "Observer.h"

namespace NicheGraphics::Drivers
{

class LatchingBacklight
{
  public:
    static LatchingBacklight *getInstance(); // Create or get the singleton instance
    void setPin(uint8_t pin, bool activeWhen = HIGH);

    int beforeDeepSleep(void *unused); // Callback for auto-shutoff

    void peek();  // Backlight on temporarily, e.g. while button held
    void latch(); // Backlight on permanently, e.g. toggled via menu
    void off();   // Backlight off. Suitable for both peek and latch

    bool isOn(); // Either peek or latch
    bool isLatched();

  private:
    LatchingBacklight(); // Constructor made private: force use of getInstance

    // Get notified when the system is shutting down
    CallbackObserver<LatchingBacklight, void *> deepSleepObserver =
        CallbackObserver<LatchingBacklight, void *>(this, &LatchingBacklight::beforeDeepSleep);

    uint8_t pin = (uint8_t)-1;
    bool logicActive = HIGH; // Is light active HIGH or active LOW

    bool on = false;      // Is light on (either peek or latched)
    bool latched = false; // Is light latched on
};

} // namespace NicheGraphics::Drivers