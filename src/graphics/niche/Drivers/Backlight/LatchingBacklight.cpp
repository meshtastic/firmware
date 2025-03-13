#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./LatchingBacklight.h"

#include "assert.h"

#include "sleep.h"

using namespace NicheGraphics::Drivers;

// Private constructor
// Called by getInstance
LatchingBacklight::LatchingBacklight()
{
    // Attach the deep sleep callback
    deepSleepObserver.observe(&notifyDeepSleep);
}

// Get access to (or create) the singleton instance of this class
LatchingBacklight *LatchingBacklight::getInstance()
{
    // Instantiate the class the first time this method is called
    static LatchingBacklight *const singletonInstance = new LatchingBacklight;

    return singletonInstance;
}

// Which pin controls the backlight?
// Is the light active HIGH (default) or active LOW?
void LatchingBacklight::setPin(uint8_t pin, bool activeWhen)
{
    this->pin = pin;
    this->logicActive = activeWhen;

    pinMode(pin, OUTPUT);
    off(); // Explicit off seem required by T-Echo?
}

// Called when device is shutting down
// Ensures the backlight is off
int LatchingBacklight::beforeDeepSleep(void *unused)
{
    // Contingency only
    // - pin wasn't set
    if (pin != (uint8_t)-1) {
        off();
        pinMode(pin, INPUT); // High impedance - unnecessary?
    } else
        LOG_WARN("LatchingBacklight instantiated, but pin not set");
    return 0; // Continue with deep sleep
}

// Turn the backlight on *temporarily*
// This should be used for momentary illumination, such as while a button is held
// The effect on the backlight is the same; peek and latch are separated to simplify short vs long press button handling
void LatchingBacklight::peek()
{
    assert(pin != (uint8_t)-1);
    digitalWrite(pin, logicActive); // On
    on = true;
    latched = false;
}

// Turn the backlight on, and keep it on
// This should be used when the backlight should remain active, even after user input ends
// e.g. when enabled via the menu
// The effect on the backlight is the same; peek and latch are separated to simplify short vs long press button handling
void LatchingBacklight::latch()
{
    assert(pin != (uint8_t)-1);

    // Blink if moving from peek to latch
    // Indicates to user that the transition has taken place
    if (on && !latched) {
        digitalWrite(pin, !logicActive); // Off
        delay(25);
        digitalWrite(pin, logicActive); // On
        delay(25);
        digitalWrite(pin, !logicActive); // Off
        delay(25);
    }

    digitalWrite(pin, logicActive); // On
    on = true;
    latched = true;
}

// Turn the backlight off
// Suitable for ending both peek and latch
void LatchingBacklight::off()
{
    assert(pin != (uint8_t)-1);
    digitalWrite(pin, !logicActive); // Off
    on = false;
    latched = false;
}

bool LatchingBacklight::isOn()
{
    return on;
}

bool LatchingBacklight::isLatched()
{
    return latched;
}

#endif
