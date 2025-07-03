#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Base class for InkHUD applets
    Must be overriden

    An applet is one "program" which may show info on the display.

*/

#pragma once

#include "configuration.h"

#include <GFX.h> // GFXRoot drawing lib

#include "mesh/MeshTypes.h"

#include "./AppletFont.h"
#include "./Applets/System/Notification/Notification.h" // The notification object, not the applet
#include "./InkHUD.h"
#include "./Persistence.h"
#include "./Tile.h"
#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

using NicheGraphics::Drivers::EInk;
using std::to_string;

class Applet : public GFX
{
  public:
    // Which edge Applet::printAt will place on the Y parameter
    enum VerticalAlignment : uint8_t {
        TOP,
        MIDDLE,
        BOTTOM,
    };

    // Which edge Applet::printAt will place on the X parameter
    enum HorizontalAlignment : uint8_t {
        LEFT,
        RIGHT,
        CENTER,
    };

    // An easy-to-understand interpretation of SNR and RSSI
    // Calculate with Applet::getSignalStrength
    enum SignalStrength : int8_t {
        SIGNAL_UNKNOWN = -1,
        SIGNAL_NONE,
        SIGNAL_BAD,
        SIGNAL_FAIR,
        SIGNAL_GOOD,
    };

    Applet();

    void setTile(Tile *t); // Should only be called via Tile::setApplet
    Tile *getTile();       // Tile with which this applet is linked

    // Rendering

    void render();                                // Draw the applet
    bool wantsToRender();                         // Check whether applet wants to render
    bool wantsToAutoshow();                       // Check whether applet wants to become foreground
    Drivers::EInk::UpdateTypes wantsUpdateType(); // Check which display update type the applet would prefer
    void updateDimensions();                      // Get current size from tile
    void resetDrawingSpace();                     // Makes sure every render starts with same parameters

    // State of the applet

    void activate();          // Begin running
    void deactivate();        // Stop running
    void bringToForeground(); // Show
    void sendToBackground();  // Hide
    bool isActive();
    bool isForeground();

    // Event handlers

    virtual void onRender() = 0; // All drawing happens here
    virtual void onActivate() {}
    virtual void onDeactivate() {}
    virtual void onForeground() {}
    virtual void onBackground() {}
    virtual void onShutdown() {}
    virtual void onButtonShortPress() {} // (System Applets only)
    virtual void onButtonLongPress() {}  // (System Applets only)

    virtual bool approveNotification(Notification &n); // Allow an applet to veto a notification

    static uint16_t getHeaderHeight(); // How tall the "standard" applet header is

    static AppletFont fontSmall, fontMedium, fontLarge; // The general purpose fonts, used by all applets

    const char *name = nullptr; // Shown in applet selection menu. Also used as an identifier by InkHUD::getSystemApplet

  protected:
    void drawPixel(int16_t x, int16_t y, uint16_t color) override; // Place a single pixel. All drawing output passes through here

    void requestUpdate(EInk::UpdateTypes type = EInk::UpdateTypes::UNSPECIFIED); // Ask WindowManager to schedule a display update
    void requestAutoshow();                                                      // Ask for applet to be moved to foreground

    uint16_t X(float f);                                                      // Map applet width, mapped from 0 to 1.0
    uint16_t Y(float f);                                                      // Map applet height, mapped from 0 to 1.0
    void setCrop(int16_t left, int16_t top, uint16_t width, uint16_t height); // Ignore pixels drawn outside a certain region
    void resetCrop();                                                         // Removes setCrop()

    // Text

    void setFont(AppletFont f);
    AppletFont getFont();
    uint16_t getTextWidth(std::string text);
    uint16_t getTextWidth(const char *text);
    uint32_t getWrappedTextHeight(int16_t left, uint16_t width, std::string text); // Result of printWrapped
    void printAt(int16_t x, int16_t y, const char *text, HorizontalAlignment ha = LEFT, VerticalAlignment va = TOP);
    void printAt(int16_t x, int16_t y, std::string text, HorizontalAlignment ha = LEFT, VerticalAlignment va = TOP);
    void printThick(int16_t xCenter, int16_t yCenter, std::string text, uint8_t thicknessX, uint8_t thicknessY); // Faux bold
    void printWrapped(int16_t left, int16_t top, uint16_t width, std::string text); // Per-word line wrapping

    void hatchRegion(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t spacing, Color color); // Fill with sparse lines
    void drawHeader(std::string text); // Draw the standard applet header

    // Meshtastic Logo

    static constexpr float LOGO_ASPECT_RATIO = 1.9;                    // Width:Height for drawing the Meshtastic logo
    uint16_t getLogoWidth(uint16_t limitWidth, uint16_t limitHeight);  // Size Meshtastic logo to fit within region
    uint16_t getLogoHeight(uint16_t limitWidth, uint16_t limitHeight); // Size Meshtastic logo to fit within region
    void drawLogo(int16_t centerX, int16_t centerY, uint16_t width, uint16_t height,
                  Color color = BLACK); // Draw the Meshtastic logo

    std::string hexifyNodeNum(NodeNum num);                    // Style as !0123abdc
    SignalStrength getSignalStrength(float snr, float rssi);   // Interpret SNR and RSSI, as an easy to understand value
    std::string getTimeString(uint32_t epochSeconds);          // Human readable
    std::string getTimeString();                               // Current time, human readable
    uint16_t getActiveNodeCount();                             // Duration determined by user, in onscreen menu
    std::string localizeDistance(uint32_t meters);             // Human readable distance, imperial or metric
    std::string parse(std::string text);                       // Handle text which might contain special chars
    std::string parseShortName(meshtastic_NodeInfoLite *node); // Get the shortname, or a substitute if has unprintable chars
    bool isPrintable(std::string);                             // Check for characters which the font can't print

    // Convenient references

    InkHUD *inkhud = nullptr;
    Persistence::Settings *settings = nullptr;
    Persistence::LatestMessage *latestMessage = nullptr;

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
    int16_t cropLeft = 0;
    int16_t cropTop = 0;
    uint16_t cropWidth = 0;
    uint16_t cropHeight = 0;
};

}; // namespace NicheGraphics::InkHUD

#endif