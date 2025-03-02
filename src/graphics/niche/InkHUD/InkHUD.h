#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    InkHUD's main class
    - singleton
    - mediator between the various components

*/

#pragma once

#include "configuration.h"

#include "./Types.h"
#include "graphics/niche/Drivers/EInk/EInk.h"

#include "./AppletFont.h"

#include <vector>

namespace NicheGraphics::InkHUD
{

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
    static InkHUD *getInstance(); // Access to this singleton class

    // Configuration
    // - before InkHUD::begin, in variant nicheGraphics.h,

    void setDriver(Drivers::EInk *driver);
    void setDisplayResilience(uint8_t fastPerFull = 5, float stressMultiplier = 2.0);
    void addApplet(const char *name, Applet *a, bool defaultActive = false, bool defaultAutoshow = false, uint8_t onTile = -1);

    void begin();

    // Handle user-button press
    // - connected to an input source, in variant nicheGraphics.h

    void shortpress();
    void longpress();

    // Trigger UI changes
    // - called by various InkHUD components
    // - suitable(?) for use by aux button, connected in variant nicheGraphics.h

    void nextApplet();
    void openMenu();
    void nextTile();
    void rotate();
    void toggleBatteryIcon();

    // Updating the display
    // - called by various InkHUD components

    void requestUpdate();
    void forceUpdate(Drivers::EInk::UpdateTypes type = Drivers::EInk::UpdateTypes::UNSPECIFIED, bool async = true);
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
    Persistence *persistence;

  private:
    InkHUD() {} // Constructor made private to force use of InkHUD::getInstance

    Events *events;               // Handle non-specific firmware events
    Renderer *renderer;           // Co-ordinate display updates
    WindowManager *windowManager; // Multiplexing of applets
};

} // namespace NicheGraphics::InkHUD

#endif