#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./TwoButton.h"

#include "PowerFsm.h"
#include "sleep.h"

using namespace NicheGraphics::Inputs;

TwoButton::TwoButton() : concurrency::OSThread("TwoButton")
{
    // Don't start polling buttons for release immediately
    // Assume they are in a "released" state at boot
    OSThread::disable();

#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
}

// Get access to (or create) the singleton instance of this class
// Accessible inside the ISRs, even though we maybe shouldn't
TwoButton *TwoButton::getInstance()
{
    // Instantiate the class the first time this method is called
    static TwoButton *const singletonInstance = new TwoButton;

    return singletonInstance;
}

// Begin receiving button input
// We probably need to do this after sleep, as well as at boot
void TwoButton::start()
{
    attachInterrupt(buttons[0].pin, TwoButton::isrPrimary, buttons[0].activeLogic == LOW ? FALLING : RISING);
    attachInterrupt(buttons[1].pin, TwoButton::isrSecondary, buttons[1].activeLogic == LOW ? FALLING : RISING);
}

// Stop receiving button input, and run custom sleep code
// Called before device sleeps. This might be power-off, or just ESP32 light sleep
// Some devices will want to attach interrupts here, for the user button to wake from sleep
void TwoButton::stop()
{
    detachInterrupt(buttons[0].pin);
    detachInterrupt(buttons[1].pin);
}

// Configures the wiring and logic of either button
// Called when outlining your NicheGraphics implementation, in variant/nicheGraphics.cpp
void TwoButton::setWiring(uint8_t whichButton, uint8_t pin, bool internalPullup)
{
    assert(whichButton < 2);
    buttons[whichButton].pin = pin;
    buttons[whichButton].activeLogic = LOW;
    buttons[whichButton].mode = internalPullup ? INPUT_PULLUP : INPUT; // fix me

    pinMode(buttons[whichButton].pin, buttons[whichButton].mode);
}

// Set what should happen when a button becomes pressed
// Use this to implement a "while held" behavior
void TwoButton::setHandlerDown(uint8_t whichButton, Callback onDown)
{
    assert(whichButton < 2);
    buttons[whichButton].onDown = onDown;
}

// Set what should happen when a button becomes unpressed
// Use this to implement a "While held" behavior
void TwoButton::setHandlerUp(uint8_t whichButton, Callback onUp)
{
    assert(whichButton < 2);
    buttons[whichButton].onUp = onUp;
}

// Set what should happen when a "short press" event has occurred
void TwoButton::setHandlerShortPress(uint8_t whichButton, Callback onShortPress)
{
    assert(whichButton < 2);
    buttons[whichButton].onShortPress = onShortPress;
}

// Set what should happen when a "long press" event has fired
// Note: this will occur while the button is still held
void TwoButton::setHandlerLongPress(uint8_t whichButton, Callback onLongPress)
{
    assert(whichButton < 2);
    buttons[whichButton].onLongPress = onLongPress;
}

// Handle the start of a press to the primary button
// Wakes our button thread
void TwoButton::isrPrimary()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButton *b = TwoButton::getInstance();
        if (b->buttons[0].state == State::REST) {
            b->buttons[0].state = State::IRQ;
            b->buttons[0].pressMs = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

// Handle the start of a press to the secondary button
// Wakes our button thread
void TwoButton::isrSecondary()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButton *b = TwoButton::getInstance();
        if (b->buttons[1].state == State::REST) {
            b->buttons[1].state = State::IRQ;
            b->buttons[1].pressMs = millis();
            b->startThread();
        }
        isrRunning = false;
    }
}

// Concise method to start our button thread
// Follows an ISR, listening for button release
void TwoButton::startThread()
{
    if (!OSThread::enabled) {
        OSThread::setInterval(50);
        OSThread::enabled = true;
    }
}

// Concise method to stop our button thread
// Called when we no longer need to poll for button release
void TwoButton::stopThread()
{
    if (OSThread::enabled) {
        OSThread::disable();
    }

    // Reset both buttons manually
    // Just in case an IRQ fires during the process of resetting the system
    // Can occur with super rapid presses?
    buttons[0].state = REST;
    buttons[1].state = REST;
}

// Our button thread
// Started by an IRQ, on either button
// Polls for button releases
// Stops when both buttons released
int32_t TwoButton::runOnce()
{
    constexpr uint8_t BUTTON_COUNT = sizeof(buttons) / sizeof(Button);
    constexpr uint8_t DEBOUNCE_MS = 50; // Ignore handle presses shorter than this - TODO, set in nichegraphics.h
    constexpr uint16_t LONGPRESS_MS = 500;

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
            buttons[i].onDown();                       // Inform that press has begun (possible hold behavior)
            buttons[i].state = State::POLLING_UNFIRED; // Mark that button-down has been handled
            awaitingRelease = true;                    // Mark that polling-for-release should continue
            break;

        // An existing press continues
        // Not held long enough to register as longpress
        case POLLING_UNFIRED: {
            uint32_t length = millis() - buttons[i].pressMs;

            // If button released since last thread tick,
            if (digitalRead(buttons[i].pin) != buttons[i].activeLogic) {
                buttons[i].onUp();              // Inform that press has ended (possible release of a hold)
                buttons[i].state = State::REST; // Mark that the button has reset
                if (length > DEBOUNCE_MS && length < LONGPRESS_MS)
                    buttons[i].onShortPress();
            }

            // If button not yet released
            else {
                awaitingRelease = true; // Mark that polling-for-release should continue
                if (length >= LONGPRESS_MS) {
                    // Raise a long press event, once
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
            awaitingRelease = true;
            if (digitalRead(buttons[i].pin) != buttons[i].activeLogic) {
                buttons[i].state = State::REST;
                buttons[i].onUp(); // Possible release of hold (in this case: *after* longpress has fired)
            }
            break;
        }
    }

    // If both buttons are now released
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
int TwoButton::beforeLightSleep(void *unused)
{
    stop();
    return 0; // Indicates success
}

// Reconfigure our interrupts
// Our class' interrupts were disconnected during sleep, to allow the user button to wake the device from sleep
int TwoButton::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    start();

    // Manually trigger the button-down ISR
    // - during light sleep, our ISR is disabled
    // - if light sleep ends by button press, pretend our own ISR caught it
    if (cause == ESP_SLEEP_WAKEUP_GPIO)
        isrPrimary();

    return 0; // Indicates success
}

#endif

#endif