#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Responsible for managing which applets are shown, and their sizes / positions

*/

#pragma once

#include "configuration.h"

#include "./Applets/System/Notification/Notification.h" // The notification object, not the applet
#include "./InkHUD.h"
#include "./Persistence.h"
#include "./Tile.h"

namespace NicheGraphics::InkHUD
{

class WindowManager
{
  public:
    WindowManager();
    void addApplet(const char *name, Applet *a, bool defaultActive, bool defaultAutoshow, uint8_t onTile);
    void begin();

    // - call these to make stuff change

    void nextTile();
    void openMenu();
    void nextApplet();
    void rotate();
    void toggleBatteryIcon();

    // - call these to manifest changes already made to the relevant Persistence::Settings values

    void changeLayout();           // Change tile layout or count
    void changeActivatedApplets(); // Change which applets are activated

    // - called during the rendering operation

    void autoshow();                     // Show a different applet, to display new info
    std::vector<Tile *> getEmptyTiles(); // Any user tiles without a valid applet

  private:
    // Steps for configuring (or reconfiguring) the window manager
    // - all steps required at startup
    // - various combinations of steps required for on-the-fly reconfiguration (by user, via menu)

    void addSystemApplet(const char *name, SystemApplet *applet, Tile *tile);
    void createSystemApplets(); // Instantiate the system applets
    void placeSystemTiles();    // Assign manual positions to (most) system applets

    void createUserApplets(); // Activate user's selected applets
    void createUserTiles();   // Instantiate enough tiles for user's selected layout
    void assignUserAppletsToTiles();
    void placeUserTiles(); // Automatically place tiles, according to user's layout
    void refocusTile();    // Ensure focused tile has a valid applet

    void findOrphanApplets(); // Find any applets left-behind when layout changes

    std::vector<Tile *> userTiles; // Tiles which can host user applets

    // For convenience
    InkHUD *inkhud = nullptr;
    Persistence::Settings *settings = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif