#pragma once

#include <cstring>

#include <OLEDDisplayUi.h>

#ifdef USE_SH1106
#include <SH1106Wire.h>
#else
#include <SSD1306Wire.h>
#endif

#include "TFT.h"
#include "TypedQueue.h"
#include "commands.h"
#include "concurrency/LockGuard.h"
#include "concurrency/PeriodicTask.h"
#include "power.h"
#include <string>

namespace graphics
{

// Forward declarations
class Screen;

/// Handles gathering and displaying debug information.
class DebugInfo
{
  public:
    DebugInfo(const DebugInfo &) = delete;
    DebugInfo &operator=(const DebugInfo &) = delete;

    /// Sets the name of the channel.
    void setChannelNameStatus(const char *name)
    {
        concurrency::LockGuard guard(&lock);
        channelName = name;
    }

  private:
    friend Screen;

    DebugInfo() {}

    /// Renders the debug screen.
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);


    std::string channelName;

    /// Protects all of internal state.
    concurrency::Lock lock;
};

/**
 * @brief This class deals with showing things on the screen of the device.
 *
 * @details Other than setup(), this class is thread-safe as long as drawFrame is not called
 *          multiple times simultaneously. All state-changing calls are queued and executed
 *          when the main loop calls us.
 */
class Screen : public concurrency::PeriodicTask
{
    CallbackObserver<Screen, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<Screen, const meshtastic::Status *>(this, &Screen::handleStatusUpdate);
    CallbackObserver<Screen, const meshtastic::Status *> gpsStatusObserver =
        CallbackObserver<Screen, const meshtastic::Status *>(this, &Screen::handleStatusUpdate);
    CallbackObserver<Screen, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<Screen, const meshtastic::Status *>(this, &Screen::handleStatusUpdate);

  public:
    Screen(uint8_t address, int sda = -1, int scl = -1);

    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;

    /// Initializes the UI, turns on the display, starts showing boot screen.
    //
    // Not thread safe - must be called before any other methods are called.
    void setup();

    /// Turns the screen on/off.
    void setOn(bool on)
    {
        if (!on)
            handleSetOn(
                false); // We handle off commands immediately, because they might be called because the CPU is shutting down
        else
            enqueueCmd(ScreenCmd{.cmd = on ? Cmd::SET_ON : Cmd::SET_OFF});
    }

    /// Handles a button press.
    void onPress() { enqueueCmd(ScreenCmd{.cmd = Cmd::ON_PRESS}); }

    // Implementation to Adjust Brightness
    void adjustBrightness();
    uint8_t brightness = 150;

    /// Starts showing the Bluetooth PIN screen.
    //
    // Switches over to a static frame showing the Bluetooth pairing screen
    // with the PIN.
    void startBluetoothPinScreen(uint32_t pin)
    {
        ScreenCmd cmd;
        cmd.cmd = Cmd::START_BLUETOOTH_PIN_SCREEN;
        cmd.bluetooth_pin = pin;
        enqueueCmd(cmd);
    }

    /// Stops showing the bluetooth PIN screen.
    void stopBluetoothPinScreen() { enqueueCmd(ScreenCmd{.cmd = Cmd::STOP_BLUETOOTH_PIN_SCREEN}); }

    /// Stops showing the boot screen.
    void stopBootScreen() { enqueueCmd(ScreenCmd{.cmd = Cmd::STOP_BOOT_SCREEN}); }

    /// Writes a string to the screen.
    void print(const char *text)
    {
        ScreenCmd cmd;
        cmd.cmd = Cmd::PRINT;
        // TODO(girts): strdup() here is scary, but we can't use std::string as
        // FreeRTOS queue is just dumbly copying memory contents. It would be
        // nice if we had a queue that could copy objects by value.
        cmd.print_text = strdup(text);
        if (!enqueueCmd(cmd)) {
            free(cmd.print_text);
        }
    }

    /// Overrides the default utf8 character conversion, to replace empty space with question marks
    static char customFontTableLookup(const uint8_t ch)
    {
        // UTF-8 to font table index converter
        // Code form http://playground.arduino.cc/Main/Utf8ascii
        static uint8_t LASTCHAR;
        static bool SKIPREST; // Only display a single unconvertable-character symbol per sequence of unconvertable characters

        if (ch < 128) { // Standard ASCII-set 0..0x7F handling
            LASTCHAR = 0;
            SKIPREST = false;
            return ch;
        }

        uint8_t last = LASTCHAR; // get last char
        LASTCHAR = ch;

        switch (last) { // conversion depnding on first UTF8-character
        case 0xC2: {
            SKIPREST = false;
            return (uint8_t)ch;
        }
        case 0xC3: {
            SKIPREST = false;
            return (uint8_t)(ch | 0xC0);
        }
        }

        // We want to strip out prefix chars for two-byte char formats
        if (ch == 0xC2 || ch == 0xC3 || ch == 0x82)
            return (uint8_t)0;

        // If we already returned an unconvertable-character symbol for this unconvertable-character sequence, return NULs for the
        // rest of it
        if (SKIPREST)
            return (uint8_t)0;
        SKIPREST = true;

        return (uint8_t)191; // otherwise: return ¿ if character can't be converted (note that the font map we're using doesn't
                             // stick to standard EASCII codes)
    }

    /// Returns a handle to the DebugInfo screen.
    //
    // Use this handle to set things like battery status, user count, GPS status, etc.
    DebugInfo *debug_info() { return &debugInfo; }

    int handleStatusUpdate(const meshtastic::Status *arg);

  protected:
    /// Updates the UI.
    //
    // Called periodically from the main loop.
    void doTask() final;

  private:
    struct ScreenCmd {
        Cmd cmd;
        union {
            uint32_t bluetooth_pin;
            char *print_text;
        };
    };

    /// Enques given command item to be processed by main loop().
    bool enqueueCmd(const ScreenCmd &cmd)
    {
        if (!useDisplay)
            return true; // claim success if our display is not in use
        else {
            bool success = cmdQueue.enqueue(cmd, 0);
            setPeriod(1); // handle ASAP
            return success;
        }
    }

    // Implementations of various commands, called from doTask().
    void handleSetOn(bool on);
    void handleOnPress();
    void handleStartBluetoothPinScreen(uint32_t pin);
    void handlePrint(const char *text);

    /// Rebuilds our list of frames (screens) to default ones.
    void setFrames();

    /// Called when debug screen is to be drawn, calls through to debugInfo.drawFrame.
    static void drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    static void drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    /// Queue of commands to execute in doTask.
    TypedQueue<ScreenCmd> cmdQueue;
    /// Whether we are using a display
    bool useDisplay = false;
    /// Whether the display is currently powered
    bool screenOn = false;
    // Whether we are showing the regular screen (as opposed to booth screen or
    // Bluetooth PIN screen)
    bool showingNormalScreen = false;

    /// Holds state for debug information
    DebugInfo debugInfo;

    /// Display device
    /** FIXME cleanup display abstraction */
#ifdef ST7735_CS
    TFTDisplay dispdev;
#elif defined(USE_SH1106)
    SH1106Wire dispdev;
#else
    SSD1306Wire dispdev;
#endif
    /// UI helper for rendering to frames and switching between them
    OLEDDisplayUi ui;
};

} // namespace graphics
