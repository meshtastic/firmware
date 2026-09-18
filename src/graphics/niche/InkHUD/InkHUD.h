#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    InkHUD's main class
    - singleton
    - mediator between the various components

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/Drivers/EInk/EInk.h"

#include "./AppletFont.h"

#include <vector>

namespace NicheGraphics::InkHUD
{

// Color, understood by display controller IC (as bit values)
// Also suitable for use as AdafruitGFX colors
enum Color : uint8_t {
    BLACK = 0,
    WHITE = 1,
};

class Applet;
class Events;
class Persistence;
class Renderer;
class SystemApplet;
class Tile;
class WindowManager;

class InkHUD
{
  public:
    using TouchEnabledProvider = bool (*)();

    static InkHUD *getInstance(); // Access to this singleton class

    // Configuration
    // - before InkHUD::begin, in variant nicheGraphics.h,

    void setDriver(Drivers::EInk *driver);
    void setDisplayResilience(uint8_t fastPerFull = 5, float stressMultiplier = 2.0);
    void addApplet(const char *name, Applet *a, bool defaultActive = false, bool defaultAutoshow = false, uint8_t onTile = -1);

    /** Show the "applying changes" holding screen.
     *
     * Called at the moment a config change is *initiated*, and it serves two distinct jobs at its
     * call sites in MenuApplet.cpp. Do not treat them alike when refactoring:
     *
     *  - a **live** change, applied without restarting (LoRa region and modem preset). The screen
     *    covers the seconds the e-ink takes to redraw. These calls must stay: nothing else raises
     *    them.
     *  - an imminent **reboot**, where it warns before the display goes. These sit next to an
     *    applyConfigChange(..., CONFIG_APPLY_REBOOT).
     *
     * Neither is redundant with anything, which is the point worth recording. MeshService's
     * requestReboot() deliberately carries no UI: BaseUI renders its own notice at draw time from
     * rebootAtMsec, whereas e-ink only draws when pushed, so InkHUD has to be told explicitly.
     * And the existing `notifyReboot` Observable (sleep.h, fired from Power::reboot) is a
     * *different moment* - reboot execution, not scheduling - which InkHUD already observes to save
     * settings and shut applets down. Centralising these calls onto a new "reboot scheduled"
     * observable would add a third reboot signal for no functional gain.
     */
    void notifyApplyingChanges();

    void begin();

    // Optional touch-state provider for reusable touch status indicators.
    void setTouchEnabledProvider(TouchEnabledProvider provider);
    bool hasTouchEnabledProvider() const;
    bool isTouchEnabled() const;

    // Handle user-button press
    // - connected to an input source, in variant nicheGraphics.h

    void shortpress();
    void longpress();
    void exitShort();
    void exitLong();
    void navUp();
    void navDown();
    void navLeft();
    void navRight();
    void touchNavUp();
    void touchNavDown();
    void touchTap(uint16_t x, uint16_t y);
    void touchLongPress(uint16_t x, uint16_t y);

    // Freetext handlers
    void freeText(char c);
    void freeTextDone();
    void freeTextCancel();

    // Trigger UI changes
    // - called by various InkHUD components
    // - suitable(?) for use by aux button, connected in variant nicheGraphics.h

    void nextApplet();
    void prevApplet();
    NicheGraphics::InkHUD::Applet *getActiveApplet();
    void openMenu();
    void openAppSwitcher();
    void openAlignStick();
    void openKeyboard();
    void closeKeyboard();
    void nextTile();
    void prevTile();
    bool showApplet(uint8_t appletIndex);
    bool selectTileAt(uint16_t x, uint16_t y);
    void rotate();
    void rotateJoystick(uint8_t angle = 1); // rotate 90 deg by default
    void toggleBatteryIcon();

    // Used by TipsApplet to force menu to start on Region selection
    bool forceRegionMenu = false;

    // Input mode hint for devices that use a left/right rocker plus center button
    bool twoWayRocker = false;

    // Updating the display
    // - called by various InkHUD components

    void requestUpdate();
    void forceUpdate(Drivers::EInk::UpdateTypes type = Drivers::EInk::UpdateTypes::UNSPECIFIED, bool all = false,
                     bool async = true);
    void awaitUpdate();

    // (Re)configuring WindowManager

    void autoshow();              // Bring an applet to foreground
    void updateAppletSelection(); // Change which applets are active
    void updateLayout();          // Change multiplexing (count, rotation)

    // Information passed between components

    uint16_t width();                    // From E-Ink driver
    uint16_t height();                   // From E-Ink driver
    std::vector<Tile *> getEmptyTiles(); // From WindowManager

    // Applets

    SystemApplet *getSystemApplet(const char *name);
    std::vector<Applet *> userApplets;
    std::vector<SystemApplet *> systemApplets;

    // Pass drawing output to Renderer
    void drawPixel(int16_t x, int16_t y, Color c);

    // Shared data which persists between boots
    Persistence *persistence = nullptr;

  private:
    InkHUD() {} // Constructor made private to force use of InkHUD::getInstance

    Events *events = nullptr;               // Handle non-specific firmware events
    Renderer *renderer = nullptr;           // Co-ordinate display updates
    WindowManager *windowManager = nullptr; // Multiplexing of applets
    TouchEnabledProvider touchEnabledProvider = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif
