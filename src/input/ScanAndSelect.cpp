#include "configuration.h"

// Normally these input methods are protected by guarding in setupModules
// In order to have the user button dismiss the canned message frame, this class lightly interacts with the Screen class
#if HAS_SCREEN

#include "ScanAndSelect.h"
#include "modules/CannedMessageModule.h"
#include <Throttle.h>
#ifdef ARCH_PORTDUINO // Only to check for pin conflict with user button
#include "platform/portduino/PortduinoGlue.h"
#endif

// Config
static const char name[] = "scanAndSelect"; // should match "allow input source" string
static constexpr uint32_t durationShortMs = 50;
static constexpr uint32_t durationLongMs = 1500;
static constexpr uint32_t durationAlertMs = 2000;

// Constructor: init base class
ScanAndSelectInput::ScanAndSelectInput() : concurrency::OSThread(name) {}

// Attempt to setup class; true if success.
// Called by setupModules method. Instance deleted if setup fails.
bool ScanAndSelectInput::init()
{
    // Short circuit: Canned messages enabled?
    if (!moduleConfig.canned_message.enabled)
        return false;

    // Short circuit: Using correct "input source"?
    // Todo: protobuf enum instead of string?
    if (strcasecmp(moduleConfig.canned_message.allow_input_source, name) != 0)
        return false;

    // Determine which pin to use for the single scan-and-select button
    // User can specify this by setting any of the inputbroker pins
    // If all values are zero, we'll assume the user *does* want GPIO0
    if (moduleConfig.canned_message.inputbroker_pin_press)
        pin = moduleConfig.canned_message.inputbroker_pin_press;
    else if (moduleConfig.canned_message.inputbroker_pin_a)
        pin = moduleConfig.canned_message.inputbroker_pin_a;
    else if (moduleConfig.canned_message.inputbroker_pin_b)
        pin = moduleConfig.canned_message.inputbroker_pin_b;
    else
        pin = 0; // GPIO 0 then

        // Short circuit: if selected pin conficts with the user button
#if defined(ARCH_PORTDUINO)
    int pinUserButton = 0;
    if (settingsMap.count(user) != 0) {
        pinUserButton = settingsMap[user];
    }
#elif defined(USERPREFS_BUTTON_PIN)
    int pinUserButton = config.device.button_gpio ? config.device.button_gpio : USERPREFS_BUTTON_PIN;
#elif defined(BUTTON_PIN)
    int pinUserButton = config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN;
#else
    int pinUserButton = config.device.button_gpio;
#endif
    if (pin == pinUserButton) {
        LOG_ERROR("ScanAndSelect conflict with user button");
        return false;
    }

    // Set-up the button
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(pin, handleChangeInterrupt, CHANGE);

    // Connect our class to the canned message module
    inputBroker->registerSource(this);

    LOG_INFO("Initialized 'Scan and Select' input for Canned Messages, using pin %d", pin);
    return true; // Init succeded
}

// Runs periodically, unless sleeping between presses
int32_t ScanAndSelectInput::runOnce()
{
    uint32_t now = millis();

    // If: "no messages added" alert screen currently shown
    if (alertingNoMessage) {
        // Dismiss the alert screen several seconds after it appears
        if (!Throttle::isWithinTimespanMs(alertingSinceMs, durationAlertMs)) {
            alertingNoMessage = false;
            screen->endAlert();
        }
    }

    // If: Button is pressed
    if (digitalRead(pin) == LOW) {
        // New press
        if (!held) {
            downSinceMs = now;
        }

        // Existing press
        else {
            // Longer than shortpress window
            // Long press not yet fired (prevent repeat firing while held)
            if (!longPressFired && !Throttle::isWithinTimespanMs(downSinceMs, durationLongMs)) {
                longPressFired = true;
                longPress();
            }
        }

        // Record the change of state: button is down
        held = true;
    }

    // If: Button is not pressed
    else {
        // Button newly released
        // Long press event didn't already fire
        if (held && !longPressFired) {
            // Duration within shortpress window
            // - longer than durationShortPress (debounce)
            // - shorter than durationLongPress
            if (!Throttle::isWithinTimespanMs(downSinceMs, durationShortMs)) {
                shortPress();
            }
        }

        // Record the change of state: button is up
        held = false;
        longPressFired = false; // Re-Arm: allow another long press
    }

    // If thread's job is done, let it sleep
    if (!held && !alertingNoMessage) {
        Thread::canSleep = true;
        return OSThread::disable();
    }

    // Run this method again is a few ms
    return durationShortMs;
}

void ScanAndSelectInput::longPress()
{
    // (If canned messages set)
    if (cannedMessageModule->hasMessages()) {
        // If module frame displayed already, send the current message
        if (cannedMessageModule->shouldDraw())
            raiseEvent(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT);

        // Otherwise, initial long press opens the module frame
        else
            raiseEvent(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN);
    }

    // (If canned messages not set) tell the user
    else
        alertNoMessage();
}

void ScanAndSelectInput::shortPress()
{
    // (If canned messages set) scroll to next message
    if (cannedMessageModule->hasMessages())
        raiseEvent(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN);

    // (If canned messages not yet set) tell the user
    else
        alertNoMessage();
}

// Begin running runOnce at regular intervals
// Called from pin change interrupt
void ScanAndSelectInput::enableThread()
{
    Thread::canSleep = false;
    OSThread::enabled = true;
    OSThread::setIntervalFromNow(0);
}

// Inform user (screen) that no canned messages have been added
// Automatically dismissed after several seconds
void ScanAndSelectInput::alertNoMessage()
{
    alertingNoMessage = true;
    alertingSinceMs = millis();

    // Graphics code: the alert frame to show on screen
    screen->startAlert([](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) -> void {
        display->setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
        display->setFont(FONT_SMALL);
        int16_t textX = display->getWidth() / 2;
        int16_t textY = display->getHeight() / 2;
        display->drawString(textX + x, textY + y, "No Canned Messages");
    });
}

// Remove the canned message frame from screen
// Used to dismiss the module frame when user button pressed
// Returns true if the frame was previously displayed, and has now been closed
// Return value consumed by Screen class when determining how to handle user button
bool ScanAndSelectInput::dismissCannedMessageFrame()
{
    if (cannedMessageModule->shouldDraw()) {
        raiseEvent(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL);
        return true;
    }

    return false;
}

// Feed input to the canned messages module
void ScanAndSelectInput::raiseEvent(_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar key)
{
    InputEvent e;
    e.source = name;
    e.inputEvent = key;
    notifyObservers(&e);
}

// Pin change interrupt
void ScanAndSelectInput::handleChangeInterrupt()
{
    // Because we need to detect both press and release (rising and falling edge), the interrupt itself can't determine the
    // action. Instead, we start up the thread and get it to read the button for us

    // The instance we're referring to here is created in setupModules()
    scanAndSelectInput->enableThread();
}

ScanAndSelectInput *scanAndSelectInput = nullptr; // Instantiated in setupModules method. Deleted if unused, or init() fails

#endif