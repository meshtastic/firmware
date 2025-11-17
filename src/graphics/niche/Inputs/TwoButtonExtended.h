#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

/*

Re-usable NicheGraphics input source

Short and Long press for up to two buttons
Interrupt driven

*/

/*

This expansion adds support for four more buttons
These buttons are single-action only, no long press
Interrupt driven

*/

#pragma once

#include "configuration.h"

#include "assert.h"
#include "functional"

#ifdef ARCH_ESP32
#include "esp_sleep.h" // For light-sleep handling
#endif

#include "Observer.h"

namespace NicheGraphics::Inputs
{

class TwoButtonExtended : protected concurrency::OSThread
{
  public:
    typedef std::function<void()> Callback;

    static uint8_t getUserButtonPin(); // Resolve the GPIO, considering the various possible source of definition

    static TwoButtonExtended *getInstance(); // Create or get the singleton instance
    void start();                            // Start handling button input
    void stop();                             // Stop handling button input (disconnect ISRs for sleep)
    void setWiring(uint8_t whichButton, uint8_t pin, bool internalPullup = false);
    void setJoystickWiring(uint8_t uPin, uint8_t dPin, uint8_t lPin, uint8_t rPin, bool internalPullup = false);
    void setTiming(uint8_t whichButton, uint32_t debounceMs, uint32_t longpressMs);
    void setJoystickDebounce(uint32_t debounceMs);
    void setHandlerDown(uint8_t whichButton, Callback onDown);
    void setHandlerUp(uint8_t whichButton, Callback onUp);
    void setHandlerShortPress(uint8_t whichButton, Callback onShortPress);
    void setHandlerLongPress(uint8_t whichButton, Callback onLongPress);
    void setJoystickDownHandlers(Callback uDown, Callback dDown, Callback ldown, Callback rDown);
    void setJoystickUpHandlers(Callback uUp, Callback dUp, Callback lUp, Callback rUp);
    void setJoystickPressHandlers(Callback uPress, Callback dPress, Callback lPress, Callback rPress);

    // Disconnect and reconnect interrupts for light sleep
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif

  private:
    // Internal state of a specific button
    enum State {
        REST,            // Up, no activity
        IRQ,             // Down detected, not yet handled
        POLLING_UNFIRED, // Down handled, polling for release
        POLLING_FIRED,   // Longpress fired, button still held
    };

    // Joystick Directions
    enum Direction { UP = 0, DOWN, LEFT, RIGHT };

    // Data used for direction (single-action) buttons
    class SimpleButton
    {
      public:
        // Per-button config
        uint8_t pin = 0xFF;                 // 0xFF: unset
        volatile State state = State::REST; // Internal state
        volatile uint32_t irqAtMillis;      // millis() when button went down

        // Per-button event callbacks
        static void noop(){};
        std::function<void()> onDown = noop;
        std::function<void()> onUp = noop;
        std::function<void()> onPress = noop;
    };

    // Data used for double-action buttons
    class Button : public SimpleButton
    {
      public:
        // Per-button extended config
        bool activeLogic = LOW;         // Active LOW by default.
        uint32_t debounceLength = 50;   // Minimum length for shortpress in ms
        uint32_t longpressLength = 500; // Time until longpress in ms

        // Per-button event callbacks
        std::function<void()> onLongPress = noop;
    };

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<TwoButtonExtended, void *> lsObserver =
        CallbackObserver<TwoButtonExtended, void *>(this, &TwoButtonExtended::beforeLightSleep);
    CallbackObserver<TwoButtonExtended, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<TwoButtonExtended, esp_sleep_wakeup_cause_t>(this, &TwoButtonExtended::afterLightSleep);
#endif

    int32_t runOnce() override; // Timer method. Polls for button release

    void startThread(); // Start polling for release
    void stopThread();  // Stop polling for release

    static void isrPrimary();   // User Button ISR
    static void isrSecondary(); // optional aux button or joystick center
    static void isrJoystickUp();
    static void isrJoystickDown();
    static void isrJoystickLeft();
    static void isrJoystickRight();

    TwoButtonExtended(); // Constructor made private: force use of Button::instance()

    // Info about both buttons
    Button buttons[2];
    bool joystickActiveLogic = LOW;       // Active LOW by default
    uint32_t joystickDebounceLength = 50; // time until press in ms
    SimpleButton joystick[4];
};

}; // namespace NicheGraphics::Inputs

#endif
