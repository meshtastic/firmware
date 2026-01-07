#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./TwoButtonExtended.h"

#include "NodeDB.h" // For the helper function TwoButtonExtended::getUserButtonPin
#include "PowerFSM.h"
#include "sleep.h"

using namespace NicheGraphics::Inputs;

TwoButtonExtended::TwoButtonExtended() : concurrency::OSThread("TwoButtonExtended")
{
    // Don't start polling buttons for release immediately
    // Assume they are in a "released" state at boot
    OSThread::disable();

#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif

    // Explicitly initialize these, just to keep cppcheck quiet..
    buttons[0] = Button();
    buttons[1] = Button();
    joystick[Direction::UP] = SimpleButton();
    joystick[Direction::DOWN] = SimpleButton();
    joystick[Direction::LEFT] = SimpleButton();
    joystick[Direction::RIGHT] = SimpleButton();
}

// Get access to (or create) the singleton instance of this class
// Accessible inside the ISRs, even though we maybe shouldn't
TwoButtonExtended *TwoButtonExtended::getInstance()
{
    // Instantiate the class the first time this method is called
    static TwoButtonExtended *const singletonInstance = new TwoButtonExtended;

    return singletonInstance;
}

// Begin receiving button input
// We probably need to do this after sleep, as well as at boot
void TwoButtonExtended::start()
{
    if (buttons[0].pin != 0xFF)
        attachInterrupt(buttons[0].pin, TwoButtonExtended::isrPrimary, buttons[0].activeLogic == LOW ? FALLING : RISING);

    if (buttons[1].pin != 0xFF)
        attachInterrupt(buttons[1].pin, TwoButtonExtended::isrSecondary, buttons[1].activeLogic == LOW ? FALLING : RISING);

    if (joystick[Direction::UP].pin != 0xFF)
        attachInterrupt(joystick[Direction::UP].pin, TwoButtonExtended::isrJoystickUp,
                        joystickActiveLogic == LOW ? FALLING : RISING);

    if (joystick[Direction::DOWN].pin != 0xFF)
        attachInterrupt(joystick[Direction::DOWN].pin, TwoButtonExtended::isrJoystickDown,
                        joystickActiveLogic == LOW ? FALLING : RISING);

    if (joystick[Direction::LEFT].pin != 0xFF)
        attachInterrupt(joystick[Direction::LEFT].pin, TwoButtonExtended::isrJoystickLeft,
                        joystickActiveLogic == LOW ? FALLING : RISING);

    if (joystick[Direction::RIGHT].pin != 0xFF)
        attachInterrupt(joystick[Direction::RIGHT].pin, TwoButtonExtended::isrJoystickRight,
                        joystickActiveLogic == LOW ? FALLING : RISING);
}

// Stop receiving button input, and run custom sleep code
// Called before device sleeps. This might be power-off, or just ESP32 light sleep
// Some devices will want to attach interrupts here, for the user button to wake from sleep
void TwoButtonExtended::stop()
{
    if (buttons[0].pin != 0xFF)
        detachInterrupt(buttons[0].pin);

    if (buttons[1].pin != 0xFF)
        detachInterrupt(buttons[1].pin);

    if (joystick[Direction::UP].pin != 0xFF)
        detachInterrupt(joystick[Direction::UP].pin);

    if (joystick[Direction::DOWN].pin != 0xFF)
        detachInterrupt(joystick[Direction::DOWN].pin);

    if (joystick[Direction::LEFT].pin != 0xFF)
        detachInterrupt(joystick[Direction::LEFT].pin);

    if (joystick[Direction::RIGHT].pin != 0xFF)
        detachInterrupt(joystick[Direction::RIGHT].pin);
}

// Attempt to resolve a GPIO pin for the user button, honoring userPrefs.jsonc and device settings
// This helper method isn't used by the TwoButtonExtended class itself, it could be moved elsewhere.
// Intention is to pass this value to TwoButtonExtended::setWiring in the setupNicheGraphics method.
uint8_t TwoButtonExtended::getUserButtonPin()
{
    uint8_t pin = 0xFF; // Unset

    // Use default pin for variant, if no better source
#ifdef BUTTON_PIN
    pin = BUTTON_PIN;
#endif

    // From userPrefs.jsonc, if set
#ifdef USERPREFS_BUTTON_PIN
    pin = USERPREFS_BUTTON_PIN;
#endif

    // From user's override in device settings, if set
    if (config.device.button_gpio)
        pin = config.device.button_gpio;

    return pin;
}

// Configures the wiring and logic of either button
// Called when outlining your NicheGraphics implementation, in variant/nicheGraphics.cpp
void TwoButtonExtended::setWiring(uint8_t whichButton, uint8_t pin, bool internalPullup)
{
    // Prevent the same GPIO being assigned to multiple buttons
    // Allows an edge case when the user remaps hardware buttons using device settings, due to a broken user button
    for (uint8_t i = 0; i < whichButton; i++) {
        if (buttons[i].pin == pin) {
            LOG_WARN("Attempted reuse of GPIO %d. Ignoring assignment whichButton=%d", pin, whichButton);
            return;
        }
    }

    assert(whichButton < 2);
    buttons[whichButton].pin = pin;
    buttons[whichButton].activeLogic = LOW;

    pinMode(buttons[whichButton].pin, internalPullup ? INPUT_PULLUP : INPUT);
}

// Configures the wiring and logic of the joystick buttons
// Called when outlining your NicheGraphics implementation, in variant/nicheGraphics.cpp
void TwoButtonExtended::setJoystickWiring(uint8_t uPin, uint8_t dPin, uint8_t lPin, uint8_t rPin, bool internalPullup)
{
    if (joystick[Direction::UP].pin == uPin || joystick[Direction::DOWN].pin == dPin || joystick[Direction::LEFT].pin == lPin ||
        joystick[Direction::RIGHT].pin == rPin) {
        LOG_WARN("Attempted reuse of Joystick GPIO. Ignoring assignment");
        return;
    }

    joystick[Direction::UP].pin = uPin;
    joystick[Direction::DOWN].pin = dPin;
    joystick[Direction::LEFT].pin = lPin;
    joystick[Direction::RIGHT].pin = rPin;
    joystickActiveLogic = LOW;

    pinMode(joystick[Direction::UP].pin, internalPullup ? INPUT_PULLUP : INPUT);
    pinMode(joystick[Direction::DOWN].pin, internalPullup ? INPUT_PULLUP : INPUT);
    pinMode(joystick[Direction::LEFT].pin, internalPullup ? INPUT_PULLUP : INPUT);
    pinMode(joystick[Direction::RIGHT].pin, internalPullup ? INPUT_PULLUP : INPUT);
}

void TwoButtonExtended::setTiming(uint8_t whichButton, uint32_t debounceMs, uint32_t longpressMs)
{
    assert(whichButton < 2);
    buttons[whichButton].debounceLength = debounceMs;
    buttons[whichButton].longpressLength = longpressMs;
}

void TwoButtonExtended::setJoystickDebounce(uint32_t debounceMs)
{
    joystickDebounceLength = debounceMs;
}

// Set what should happen when a button becomes pressed
// Use this to implement a "while held" behavior
void TwoButtonExtended::setHandlerDown(uint8_t whichButton, Callback onDown)
{
    assert(whichButton < 2);
    buttons[whichButton].onDown = onDown;
}

// Set what should happen when a button becomes unpressed
// Use this to implement a "While held" behavior
void TwoButtonExtended::setHandlerUp(uint8_t whichButton, Callback onUp)
{
    assert(whichButton < 2);
    buttons[whichButton].onUp = onUp;
}

// Set what should happen when a "short press" event has occurred
void TwoButtonExtended::setHandlerShortPress(uint8_t whichButton, Callback onPress)
{
    assert(whichButton < 2);
    buttons[whichButton].onPress = onPress;
}

// Set what should happen when a "long press" event has fired
// Note: this will occur while the button is still held
void TwoButtonExtended::setHandlerLongPress(uint8_t whichButton, Callback onLongPress)
{
    assert(whichButton < 2);
    buttons[whichButton].onLongPress = onLongPress;
}

// Set what should happen when a joystick button becomes pressed
// Use this to implement a "while held" behavior
void TwoButtonExtended::setJoystickDownHandlers(Callback uDown, Callback dDown, Callback lDown, Callback rDown)
{
    joystick[Direction::UP].onDown = uDown;
    joystick[Direction::DOWN].onDown = dDown;
    joystick[Direction::LEFT].onDown = lDown;
    joystick[Direction::RIGHT].onDown = rDown;
}

// Set what should happen when a joystick button becomes unpressed
// Use this to implement a "while held" behavior
void TwoButtonExtended::setJoystickUpHandlers(Callback uUp, Callback dUp, Callback lUp, Callback rUp)
{
    joystick[Direction::UP].onUp = uUp;
    joystick[Direction::DOWN].onUp = dUp;
    joystick[Direction::LEFT].onUp = lUp;
    joystick[Direction::RIGHT].onUp = rUp;
}

// Set what should happen when a "press" event has fired
// Note: this will occur while the joystick button is still held
void TwoButtonExtended::setJoystickPressHandlers(Callback uPress, Callback dPress, Callback lPress, Callback rPress)
{
    joystick[Direction::UP].onPress = uPress;
    joystick[Direction::DOWN].onPress = dPress;
    joystick[Direction::LEFT].onPress = lPress;
    joystick[Direction::RIGHT].onPress = rPress;
}

// Handle the start of a press to the primary button
// Wakes our button thread
void TwoButtonExtended::isrPrimary()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButtonExtended *b = TwoButtonExtended::getInstance();
        if (b->buttons[0].state == State::REST) {
            b->buttons[0].state = State::IRQ;
            b->buttons[0].irqAtMillis = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

// Handle the start of a press to the secondary button
// Wakes our button thread
void TwoButtonExtended::isrSecondary()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButtonExtended *b = TwoButtonExtended::getInstance();
        if (b->buttons[1].state == State::REST) {
            b->buttons[1].state = State::IRQ;
            b->buttons[1].irqAtMillis = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

// Handle the start of a press to the joystick buttons
// Also wakes our button thread
void TwoButtonExtended::isrJoystickUp()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButtonExtended *b = TwoButtonExtended::getInstance();
        if (b->joystick[Direction::UP].state == State::REST) {
            b->joystick[Direction::UP].state = State::IRQ;
            b->joystick[Direction::UP].irqAtMillis = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

void TwoButtonExtended::isrJoystickDown()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButtonExtended *b = TwoButtonExtended::getInstance();
        if (b->joystick[Direction::DOWN].state == State::REST) {
            b->joystick[Direction::DOWN].state = State::IRQ;
            b->joystick[Direction::DOWN].irqAtMillis = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

void TwoButtonExtended::isrJoystickLeft()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButtonExtended *b = TwoButtonExtended::getInstance();
        if (b->joystick[Direction::LEFT].state == State::REST) {
            b->joystick[Direction::LEFT].state = State::IRQ;
            b->joystick[Direction::LEFT].irqAtMillis = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

void TwoButtonExtended::isrJoystickRight()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButtonExtended *b = TwoButtonExtended::getInstance();
        if (b->joystick[Direction::RIGHT].state == State::REST) {
            b->joystick[Direction::RIGHT].state = State::IRQ;
            b->joystick[Direction::RIGHT].irqAtMillis = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

// Concise method to start our button thread
// Follows an ISR, listening for button release
void TwoButtonExtended::startThread()
{
    if (!OSThread::enabled) {
        OSThread::setInterval(10);
        OSThread::enabled = true;
    }
}

// Concise method to stop our button thread
// Called when we no longer need to poll for button release
void TwoButtonExtended::stopThread()
{
    if (OSThread::enabled) {
        OSThread::disable();
    }

    // Reset both buttons manually
    // Just in case an IRQ fires during the process of resetting the system
    // Can occur with super rapid presses?
    buttons[0].state = REST;
    buttons[1].state = REST;
    joystick[Direction::UP].state = REST;
    joystick[Direction::DOWN].state = REST;
    joystick[Direction::LEFT].state = REST;
    joystick[Direction::RIGHT].state = REST;
}

// Our button thread
// Started by an IRQ, on either button
// Polls for button releases
// Stops when both buttons released
int32_t TwoButtonExtended::runOnce()
{
    constexpr uint8_t BUTTON_COUNT = sizeof(buttons) / sizeof(Button);
    constexpr uint8_t JOYSTICK_COUNT = sizeof(joystick) / sizeof(SimpleButton);

    // Allow either button to request that our thread should continue polling
    bool awaitingRelease = false;

    // Check both primary and secondary buttons
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        switch (buttons[i].state) {
        // No action: button has not been pressed
        case REST:
            break;

        // New press detected by interrupt
        case IRQ:
            powerFSM.trigger(EVENT_PRESS);             // Tell PowerFSM that press occurred (resets sleep timer)
            buttons[i].onDown();                       // Run callback: press has begun (possible hold behavior)
            buttons[i].state = State::POLLING_UNFIRED; // Mark that button-down has been handled
            awaitingRelease = true;                    // Mark that polling-for-release should continue
            break;

        // An existing press continues
        // Not held long enough to register as longpress
        case POLLING_UNFIRED: {
            uint32_t length = millis() - buttons[i].irqAtMillis;

            // If button released since last thread tick,
            if (digitalRead(buttons[i].pin) != buttons[i].activeLogic) {
                buttons[i].onUp();              // Run callback: press has ended (possible release of a hold)
                buttons[i].state = State::REST; // Mark that the button has reset
                if (length > buttons[i].debounceLength && length < buttons[i].longpressLength) // If too short for longpress,
                    buttons[i].onPress();                                                      // Run callback: press
            }
            // If button not yet released
            else {
                awaitingRelease = true; // Mark that polling-for-release should continue
                if (length >= buttons[i].longpressLength) {
                    // Run callback: long press (once)
                    // Then continue waiting for release, to rearm
                    buttons[i].state = State::POLLING_FIRED;
                    buttons[i].onLongPress();
                }
            }
            break;
        }

        // Button still held, but duration long enough that longpress event already fired
        // Just waiting for release
        case POLLING_FIRED:
            // Release detected
            if (digitalRead(buttons[i].pin) != buttons[i].activeLogic) {
                buttons[i].state = State::REST;
                buttons[i].onUp(); // Callback: release of hold (in this case: *after* longpress has fired)
            }
            // Not yet released, keep polling
            else
                awaitingRelease = true;
            break;
        }
    }

    // Check all the joystick directions
    for (uint8_t i = 0; i < JOYSTICK_COUNT; i++) {
        switch (joystick[i].state) {
        // No action: button has not been pressed
        case REST:
            break;

        // New press detected by interrupt
        case IRQ:
            powerFSM.trigger(EVENT_PRESS);              // Tell PowerFSM that press occurred (resets sleep timer)
            joystick[i].onDown();                       // Run callback: press has begun (possible hold behavior)
            joystick[i].state = State::POLLING_UNFIRED; // Mark that button-down has been handled
            awaitingRelease = true;                     // Mark that polling-for-release should continue
            break;

        // An existing press continues
        // Not held long enough to register as press
        case POLLING_UNFIRED: {
            uint32_t length = millis() - joystick[i].irqAtMillis;

            // If button released since last thread tick,
            if (digitalRead(joystick[i].pin) != joystickActiveLogic) {
                joystick[i].onUp();              // Run callback: press has ended (possible release of a hold)
                joystick[i].state = State::REST; // Mark that the button has reset
            }
            // If button not yet released
            else {
                awaitingRelease = true; // Mark that polling-for-release should continue
                if (length >= joystickDebounceLength) {
                    // Run callback: long press (once)
                    // Then continue waiting for release, to rearm
                    joystick[i].state = State::POLLING_FIRED;
                    joystick[i].onPress();
                }
            }
            break;
        }

        // Button still held after press
        // Just waiting for release
        case POLLING_FIRED:
            // Release detected
            if (digitalRead(joystick[i].pin) != joystickActiveLogic) {
                joystick[i].state = State::REST;
                joystick[i].onUp(); // Callback: release of hold
            }
            // Not yet released, keep polling
            else
                awaitingRelease = true;
            break;
        }
    }

    // If all buttons are now released
    // we don't need to waste cpu resources polling
    // IRQ will restart this thread when we next need it
    if (!awaitingRelease)
        stopThread();

    // Run this method again, or don't..
    // Use whatever behavior was previously set by stopThread() or startThread()
    return OSThread::interval;
}

#ifdef ARCH_ESP32

// Detach our class' interrupts before lightsleep
// Allows sleep.cpp to configure its own interrupts, which wake the device on user-button press
int TwoButtonExtended::beforeLightSleep(void *unused)
{
    stop();
    return 0; // Indicates success
}

// Reconfigure our interrupts
// Our class' interrupts were disconnected during sleep, to allow the user button to wake the device from sleep
int TwoButtonExtended::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    start();

    // Manually trigger the button-down ISR
    // - during light sleep, our ISR is disabled
    // - if light sleep ends by button press, pretend our own ISR caught it
    // - need to manually confirm by reading pin ourselves, to avoid occasional false positives
    //   (false positive only when using internal pullup resistors?)
    if (cause == ESP_SLEEP_WAKEUP_GPIO && digitalRead(buttons[0].pin) == buttons[0].activeLogic)
        isrPrimary();

    return 0; // Indicates success
}

#endif

#endif
