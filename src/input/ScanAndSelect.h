/*
    A "single button" input method for Canned Messages

    - Short press to cycle through messages
    - Long Press to send

    To use:
        - set "allow input source" to "scanAndSelect"
        - set the single button's GPIO as either pin A, pin B, or pin Press

    Originally designed to make use of "extra" built-in button on some boards.
    Non-intrusive; suitable for use as a default module config.
*/

#pragma once
#include "concurrency/OSThread.h"
#include "main.h"

// Normally these input methods are protected by guarding in setupModules
// In order to have the user button dismiss the canned message frame, this class lightly interacts with the Screen class
#if HAS_SCREEN

class ScanAndSelectInput : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    ScanAndSelectInput();             // No-op constructor, only initializes OSThread base class
    bool init();                      // Attempt to setup class; true if success. Instance deleted if setup fails
    bool dismissCannedMessageFrame(); // Remove the canned message frame from screen. True if frame was open, and now closed.
    void alertNoMessage();            // Inform user (screen) that no canned messages have been added

  protected:
    int32_t runOnce() override;          // Runs at regular intervals, when enabled
    void enableThread();                 // Begin running runOnce at regular intervals
    static void handleChangeInterrupt(); // Calls enableThread from pin change interrupt
    void shortPress();                   // Code to run when short press fires
    void longPress();                    // Code to run when long press fires
    void raiseEvent(_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar key); // Feed input to canned message module

    bool held = false;           // Have we handled a change in button state?
    bool longPressFired = false; // Long press fires while button still held. This bool ensures the release is no-op
    uint32_t downSinceMs = 0;    // Debouncing for short press, timing for long press
    uint8_t pin = -1;            // Read from cannned message config during init

    bool alertingNoMessage = false; // Is the "no canned messages" alert shown on screen?
    uint32_t alertingSinceMs = 0;   // Used to dismiss the "no canned message" alert several seconds
};

extern ScanAndSelectInput *scanAndSelectInput; // Instantiated in setupModules method. Deleted if unused, or init() fails

#endif