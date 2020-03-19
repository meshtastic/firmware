#pragma once

#include <cstring>

#include <OLEDDisplayUi.h>
#include <SSD1306Wire.h>

#include "PeriodicTask.h"
#include "TypedQueue.h"

namespace meshtastic
{

/// Deals with showing things on the screen of the device.
//
// Other than setup(), this class is thread-safe. All state-changing calls are
// queued and executed when the main loop calls us.
class Screen : public PeriodicTask
{
  public:
    Screen(uint8_t address, uint8_t sda, uint8_t scl);

    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;

    /// Initializes the UI, turns on the display, starts showing boot screen.
    //
    // Not thread safe - must be called before any other methods are called.
    void setup();

    /// Turns the screen on/off.
    void setOn(bool on) { enqueueCmd(CmdItem{.cmd = on ? Cmd::SET_ON : Cmd::SET_OFF}); }

    /// Handles a button press.
    void onPress() { enqueueCmd(CmdItem{.cmd = Cmd::ON_PRESS}); }

    /// Starts showing the Bluetooth PIN screen.
    //
    // Switches over to a static frame showing the Bluetooth pairing screen
    // with the PIN.
    void startBluetoothPinScreen(uint32_t pin)
    {
        CmdItem cmd;
        cmd.cmd = Cmd::START_BLUETOOTH_PIN_SCREEN;
        cmd.bluetooth_pin = pin;
        enqueueCmd(cmd);
    }

    /// Stops showing the bluetooth PIN screen.
    void stopBluetoothPinScreen() { enqueueCmd(CmdItem{.cmd = Cmd::STOP_BLUETOOTH_PIN_SCREEN}); }

    /// Stops showing the boot screen.
    void stopBootScreen() { enqueueCmd(CmdItem{.cmd = Cmd::STOP_BOOT_SCREEN}); }

    /// Writes a string to the screen.
    void print(const char *text)
    {
        CmdItem cmd;
        cmd.cmd = Cmd::PRINT;
        // TODO(girts): strdup() here is scary, but we can't use std::string as
        // FreeRTOS queue is just dumbly copying memory contents. It would be
        // nice if we had a queue that could copy objects by value.
        cmd.print_text = strdup(text);
        if (!enqueueCmd(cmd)) {
            free(cmd.print_text);
        }
    }

  protected:
    /// Updates the UI.
    //
    // Called periodically from the main loop.
    void doTask() final;

  private:
    enum class Cmd {
        INVALID,
        SET_ON,
        SET_OFF,
        ON_PRESS,
        START_BLUETOOTH_PIN_SCREEN,
        STOP_BLUETOOTH_PIN_SCREEN,
        STOP_BOOT_SCREEN,
        PRINT,
    };
    struct CmdItem {
        Cmd cmd;
        union {
            uint32_t bluetooth_pin;
            char *print_text;
        };
    };

    /// Enques given command item to be processed by main loop().
    bool enqueueCmd(const CmdItem &cmd)
    {
        bool success = cmdQueue.enqueue(cmd, 0);
        setPeriod(1); // handle ASAP
        return success;
    }

    // Implementations of various commands, called from doTask().
    void handleSetOn(bool on);
    void handleOnPress();
    void handleStartBluetoothPinScreen(uint32_t pin);
    void handlePrint(const char *text);

    /// Rebuilds our list of frames (screens) to default ones.
    void setFrames();

    /// Queue of commands to execute in doTask.
    TypedQueue<CmdItem> cmdQueue;
    /// Whether we are using a display
    bool useDisplay = false;
    /// Whether the display is currently powered
    bool screenOn = false;
    // Whether we are showing the regular screen (as opposed to booth screen or
    // Bluetooth PIN screen)
    bool showingNormalScreen = false;
    /// Display device
    SSD1306Wire dispdev;
    /// UI helper for rendering to frames and switching between them
    OLEDDisplayUi ui;
};

} // namespace meshtastic
