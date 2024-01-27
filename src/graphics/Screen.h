#pragma once

#include "configuration.h"

#include "detect/ScanI2C.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include <OLEDDisplay.h>

#if !HAS_SCREEN
#include "power.h"
namespace graphics
{
// Noop class for boards without screen.
class Screen
{
  public:
    explicit Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY);
    void onPress() {}
    void setup() {}
    void setOn(bool) {}
    void print(const char *) {}
    void doDeepSleep() {}
    void forceDisplay() {}
    void startBluetoothPinScreen(uint32_t pin) {}
    void stopBluetoothPinScreen() {}
    void startRebootScreen() {}
    void startShutdownScreen() {}
    void startFirmwareUpdateScreen() {}
};
} // namespace graphics
#else
#include <cstring>

#include <OLEDDisplayUi.h>

#include "../configuration.h"

#ifdef USE_ST7567
#include <ST7567Wire.h>
#elif defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SH1107_128_64)
#include <SH1106Wire.h>
#elif defined(USE_SSD1306)
#include <SSD1306Wire.h>
#else
// the SH1106/SSD1306 variant is auto-detected
#include <AutoOLEDWire.h>
#endif

#include "EInkDisplay2.h"
#include "TFTDisplay.h"
#include "TypedQueue.h"
#include "commands.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "mesh/MeshModule.h"
#include "power.h"
#include <string>

// 0 to 255, though particular variants might define different defaults
#ifndef BRIGHTNESS_DEFAULT
#define BRIGHTNESS_DEFAULT 150
#endif

// Meters to feet conversion
#ifndef METERS_TO_FEET
#define METERS_TO_FEET 3.28
#endif

// Feet to miles conversion
#ifndef MILES_TO_FEET
#define MILES_TO_FEET 5280
#endif

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

  private:
    friend Screen;

    DebugInfo() {}

    /// Renders the debug screen.
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    void drawFrameWiFi(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

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
class Screen : public concurrency::OSThread
{
    CallbackObserver<Screen, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<Screen, const meshtastic::Status *>(this, &Screen::handleStatusUpdate);
    CallbackObserver<Screen, const meshtastic::Status *> gpsStatusObserver =
        CallbackObserver<Screen, const meshtastic::Status *>(this, &Screen::handleStatusUpdate);
    CallbackObserver<Screen, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<Screen, const meshtastic::Status *>(this, &Screen::handleStatusUpdate);
    CallbackObserver<Screen, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<Screen, const meshtastic_MeshPacket *>(this, &Screen::handleTextMessage);
    CallbackObserver<Screen, const UIFrameEvent *> uiFrameEventObserver =
        CallbackObserver<Screen, const UIFrameEvent *>(this, &Screen::handleUIFrameEvent);
    CallbackObserver<Screen, const InputEvent *> inputObserver =
        CallbackObserver<Screen, const InputEvent *>(this, &Screen::handleInputEvent);

  public:
    explicit Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY);

    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;

    ScanI2C::DeviceAddress address_found;
    meshtastic_Config_DisplayConfig_OledType model;
    OLEDDISPLAY_GEOMETRY geometry;

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

    /**
     * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
     * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
     */
    void doDeepSleep();

    void blink();

    /// Handle button press, trackball or swipe action)
    void onPress() { enqueueCmd(ScreenCmd{.cmd = Cmd::ON_PRESS}); }
    void showPrevFrame() { enqueueCmd(ScreenCmd{.cmd = Cmd::SHOW_PREV_FRAME}); }
    void showNextFrame() { enqueueCmd(ScreenCmd{.cmd = Cmd::SHOW_NEXT_FRAME}); }

    // Implementation to Adjust Brightness
    uint8_t brightness = BRIGHTNESS_DEFAULT;

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

    void startFirmwareUpdateScreen()
    {
        ScreenCmd cmd;
        cmd.cmd = Cmd::START_FIRMWARE_UPDATE_SCREEN;
        enqueueCmd(cmd);
    }

    void startShutdownScreen()
    {
        ScreenCmd cmd;
        cmd.cmd = Cmd::START_SHUTDOWN_SCREEN;
        enqueueCmd(cmd);
    }

    void startRebootScreen()
    {
        ScreenCmd cmd;
        cmd.cmd = Cmd::START_REBOOT_SCREEN;
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

    /// generates a very brief time delta display
    std::string drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds);

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

        switch (last) { // conversion depending on first UTF8-character
        case 0xC2: {
            SKIPREST = false;
            return (uint8_t)ch;
        }
        case 0xC3: {
            SKIPREST = false;
            return (uint8_t)(ch | 0xC0);
        }
        // map UTF-8 cyrillic chars to it Windows-1251 (CP-1251) ASCII codes
        // note: in this case we must use compatible font - provided ArialMT_Plain_10/16/24 by 'ThingPulse/esp8266-oled-ssd1306'
        // library have empty chars for non-latin ASCII symbols
        case 0xD0: {
            SKIPREST = false;
            if (ch == 132)
                return (uint8_t)(170); // Є
            if (ch == 134)
                return (uint8_t)(178); // І
            if (ch == 135)
                return (uint8_t)(175); // Ї
            if (ch == 129)
                return (uint8_t)(168); // Ё
            if (ch > 143 && ch < 192)
                return (uint8_t)(ch + 48);
            break;
        }
        case 0xD1: {
            SKIPREST = false;
            if (ch == 148)
                return (uint8_t)(186); // є
            if (ch == 150)
                return (uint8_t)(179); // і
            if (ch == 151)
                return (uint8_t)(191); // ї
            if (ch == 145)
                return (uint8_t)(184); // ё
            if (ch > 127 && ch < 144)
                return (uint8_t)(ch + 112);
            break;
        }
        case 0xD2: {
            SKIPREST = false;
            if (ch == 144)
                return (uint8_t)(165); // Ґ
            if (ch == 145)
                return (uint8_t)(180); // ґ
            break;
        }
        }

        // We want to strip out prefix chars for two-byte char formats
        if (ch == 0xC2 || ch == 0xC3 || ch == 0x82 || ch == 0xD0 || ch == 0xD1)
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

    // Handle observer events
    int handleStatusUpdate(const meshtastic::Status *arg);
    int handleTextMessage(const meshtastic_MeshPacket *arg);
    int handleUIFrameEvent(const UIFrameEvent *arg);
    int handleInputEvent(const InputEvent *arg);

    /// Used to force (super slow) eink displays to draw critical frames
    void forceDisplay();

    /// Draws our SSL cert screen during boot (called from WebServer)
    void setSSLFrames();

    void setWelcomeFrames();

  protected:
    /// Updates the UI.
    //
    // Called periodically from the main loop.
    int32_t runOnce() final;

    bool isAUTOOled = false;

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
            enabled = true; // handle ASAP (we are the registered reader for cmdQueue, but might have been disabled)
            return success;
        }
    }

    // Implementations of various commands, called from doTask().
    void handleSetOn(bool on);
    void handleOnPress();
    void handleShowNextFrame();
    void handleShowPrevFrame();
    void handleStartBluetoothPinScreen(uint32_t pin);
    void handlePrint(const char *text);
    void handleStartFirmwareUpdateScreen();
    void handleShutdownScreen();
    void handleRebootScreen();
    /// Rebuilds our list of frames (screens) to default ones.
    void setFrames();

    /// Try to start drawing ASAP
    void setFastFramerate();

    // Sets frame up for immediate drawing
    void setFrameImmediateDraw(FrameCallback *drawFrames);

    /// Called when debug screen is to be drawn, calls through to debugInfo.drawFrame.
    static void drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    static void drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    static void drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

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
    OLEDDisplay *dispdev;

    /// UI helper for rendering to frames and switching between them
    OLEDDisplayUi *ui;
};

} // namespace graphics
#endif