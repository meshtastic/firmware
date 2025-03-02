#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Base class for InkHUD applets
    Must be overriden

    An applet is one "program" which may show info on the display.

    ===================================
    Preliminary notes, for the curious
    ===================================

    (This info to be streamlined, and moved to a more official documentation)

    User Applets vs System Applets
    -------------------------------

    There are either "User Applets", or "System Applets".
    This concept is only for our understanding; as far at the code is concerned, both are just "Applets"

    User applets are the "normal" applets.
    User applets are applets like "AllMessageApplet", or "MapApplet".
    User applets may be enabled / disabled by user, via the on-screen menu.
    Incorporating new UserApplets is easy: just add them during setupNicheGraphics
    If a UserApplet is not added during setupNicheGraphics, it will not be built.
    The set of available UserApplets is allowed to vary from device to device.


    Examples of system applets include "NotificationApplet" and "MenuApplet".
    For their own reasons, system applets each require some amount of special handling.

    Drawing
    --------

    *All* drawing must be performed by an Applet.
    Applets implement the onRender() method, where all drawing takes place.
    Applets are told how wide and tall they are, and are expected to draw to suit this size.
    When an applet draws, it uses co-ordinates in "Applet Space": between 0 and applet width/height.

    Event-driven rendering
    -----------------------

    Applets don't render unless something on the display needs to change.
    An applet is expected to determine for itself when it has new info to display.
    It should interact with the firmware via the MeshModule API, via Observables, etc.
    Please don't directly add hooks throughout the existing firmware code.

    When an applet decides it would like to update the display, it should call requestUpdate()
    The WindowManager will shortly call the onRender() method for all affected applets

    An Applet may be unexpectedly asked to render at any point in time.

    Applets should cache their data, but not their pixel output: they should re-render when onRender runs.
    An Applet's dimensions are not know until onRender is called, so pre-rendering of UI elements is prohibited.

    Tiles
    -----

    Applets are assigned to "Tiles".
    Assigning an applet to a tile creates a reciprocal link between the two.
    When an applet renders, it passes pixels to its tile.
    The tile translates these to the correct position, to be placed into the fullscreen framebuffer.
    User applets don't get to choose their own tile; the multiplexing is handled by the WindowManager.
    System applets might do strange things though.

    Foreground and Background
    -------------------------

    The user can cycle between applets by short-pressing the user button.
    Any applets which are currently displayed on the display are "foreground".
    When the user button is short pressed, and an applet is hidden, it becomes "background".

    Although the WindowManager will not render background applets, they should still collect data,
    so they are ready to display when they are brought to foreground again.
    Even if they are in background, Applets should still request updates when an event affects them,
    as the user may have given them permission to "autoshow"; bringing themselves foreground automatically

    Applets can implement the onForeground and onBackground methods to handle this change in state.
    They can also check their state by calling isForeground() at any time.

    Active and Inactive
    -------------------

    The user can select which applets are available, using the onscreen applet selection menu.
    Applets which are enabled in this menu are "active"; otherwise they are "inactive".

    An inactive applet is expected not collect data; not to consume resources.
    Applets are activated at boot, or when enabled via the menu.
    They are deactivated at shutdown, or when disabled via the menu.

    Applets can implement the onActivation and onDeactivation methods to handle this change in state.

*/

#pragma once

#include "configuration.h"

#include <GFX.h>

#include "./AppletFont.h"
#include "./Applets/System/Notification/Notification.h"
#include "./Tile.h"
#include "./Types.h"
#include "./WindowManager.h"
#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

using NicheGraphics::Drivers::EInk;
using std::to_string;

class Tile;
class WindowManager;

class Applet : public GFX
{
  public:
    Applet();

    void setTile(Tile *t); // Applets draw via a tile (for multiplexing)
    Tile *getTile();

    void render();
    bool wantsToRender();   // Check whether applet wants to render
    bool wantsToAutoshow(); // Check whether applets wants to become foreground, to show new data, if permitted
    Drivers::EInk::UpdateTypes wantsUpdateType(); // Check which display update type the applet would prefer
    void updateDimensions();                      // Get current size from tile
    void resetDrawingSpace();                     // Makes sure every render starts with same parameters

    // Change the applet's state

    void activate();
    void deactivate();
    void bringToForeground();
    void sendToBackground();

    // Info about applet's state

    bool isActive();
    bool isForeground();

    // Allow derived applets to handle changes in state

    virtual void onRender() = 0; // All drawing happens here
    virtual void onActivate() {}
    virtual void onDeactivate() {}
    virtual void onForeground() {}
    virtual void onBackground() {}
    virtual void onShutdown() {}
    virtual void onButtonShortPress() {} // For use by System Applets only
    virtual void onButtonLongPress() {}  // For use by System Applets only
    virtual void onLockAvailable() {}    // For use by System Applets only

    virtual bool approveNotification(Notification &n); // Allow an applet to veto a notification

    static void setDefaultFonts(AppletFont large, AppletFont small); // Set the general purpose fonts
    static uint16_t getHeaderHeight();                               // How tall is the "standard" applet header

    const char *name = nullptr; // Shown in applet selection menu

  protected:
    // Place a single pixel. All drawing methods output through here
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;

    // Tell WindowManager to update display
    void requestUpdate(EInk::UpdateTypes type = EInk::UpdateTypes::UNSPECIFIED);

    // Ask for applet to be moved to foreground
    void requestAutoshow();

    uint16_t X(float f);                                                      // Map applet width, mapped from 0 to 1.0
    uint16_t Y(float f);                                                      // Map applet height, mapped from 0 to 1.0
    void setCrop(int16_t left, int16_t top, uint16_t width, uint16_t height); // Ignore pixels drawn outside a certain region
    void resetCrop();                                                         // Removes setCrop()

    void setFont(AppletFont f);
    AppletFont getFont();

    uint16_t getTextWidth(std::string text);
    uint16_t getTextWidth(const char *text);

    void printAt(int16_t x, int16_t y, const char *text, HorizontalAlignment ha = LEFT, VerticalAlignment va = TOP);
    void printAt(int16_t x, int16_t y, std::string text, HorizontalAlignment ha = LEFT, VerticalAlignment va = TOP);
    void printThick(int16_t xCenter, int16_t yCenter, std::string text, uint8_t thicknessX, uint8_t thicknessY);

    // Print text, with per-word line wrapping
    void printWrapped(int16_t left, int16_t top, uint16_t width, std::string text);
    uint32_t getWrappedTextHeight(int16_t left, uint16_t width, std::string text);

    void hatchRegion(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t spacing, Color color); // Fill with sparse lines
    void drawHeader(std::string text); // Draw the standard applet header

    static constexpr float LOGO_ASPECT_RATIO = 1.9;                    // Width:Height for drawing the Meshtastic logo
    uint16_t getLogoWidth(uint16_t limitWidth, uint16_t limitHeight);  // Size Meshtastic logo to fit within region
    uint16_t getLogoHeight(uint16_t limitWidth, uint16_t limitHeight); // Size Meshtastic logo to fit within region
    void drawLogo(int16_t centerX, int16_t centerY, uint16_t width, uint16_t height); // Draw the meshtastic logo

    std::string hexifyNodeNum(NodeNum num);
    SignalStrength getSignalStrength(float snr, float rssi); // Interpret SNR and RSSI, as an easy to understand value
    std::string getTimeString(uint32_t epochSeconds);        // Human readable
    std::string getTimeString();                             // Current time, human readable
    uint16_t getActiveNodeCount();                           // Duration determined by user, in onscreen menu
    std::string localizeDistance(uint32_t meters);           // Human readable distance, imperial or metric

    static AppletFont fontSmall, fontLarge; // General purpose fonts, used cross-applet

  private:
    Tile *assignedTile = nullptr; // Rendered pixels are fed into a Tile object, which translates them, then passes to WM
    bool active = false;          // Has the user enabled this applet (at run-time)?
    bool foreground = false;      // Is the applet currently drawn on a tile?

    bool wantRender = false;   // In some situations, checked by WindowManager when updating, to skip unneeded redrawing.
    bool wantAutoshow = false; // Does the applet have new data it would like to display in foreground?
    NicheGraphics::Drivers::EInk::UpdateTypes wantUpdateType =
        NicheGraphics::Drivers::EInk::UpdateTypes::UNSPECIFIED; // Which update method we'd prefer when redrawing the display

    using GFX::setFont;     // Make sure derived classes use AppletFont instead of AdafruitGFX fonts directly
    using GFX::setRotation; // Block setRotation calls. Rotation is handled globally by WindowManager.

    AppletFont currentFont; // As passed to setFont

    // As set by setCrop
    int16_t cropLeft;
    int16_t cropTop;
    uint16_t cropWidth;
    uint16_t cropHeight;
};

}; // namespace NicheGraphics::InkHUD

#endif