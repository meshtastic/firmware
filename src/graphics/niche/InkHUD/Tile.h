#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Class which represents a region of the display area
    Applets are assigned to a tile
    Tile controls the Applet's dimensions
    Tile receives pixel output from the applet, and translates it to the correct display region

*/

#pragma once

#include "configuration.h"

#include "./Applet.h"
#include "./Types.h"
#include "./WindowManager.h"

#include <GFX.h>

namespace NicheGraphics::InkHUD
{

class Applet;
class WindowManager;

class Tile
{
  public:
    Tile();
    void placeUserTile(uint8_t layoutSize, uint8_t tileIndex); // Assign region automatically, based on layout
    void placeSystemTile(int16_t left, int16_t top, uint16_t width, uint16_t height); // Assign region manually
    void handleAppletPixel(int16_t x, int16_t y, Color c);                            // Receive px output from assigned applet
    uint16_t getWidth();                   // Used to set the assigned applet's width before render
    uint16_t getHeight();                  // Used to set the assigned applet's height before render
    static uint16_t maxDisplayDimension(); // Largest possible width / height any tile may ever encounter

    void assignApplet(Applet *a); // Place an applet onto a tile
    Applet *getAssignedApplet();  // Applet which is on a tile

    void requestHighlight();              // Ask for this tile to be highlighted
    static void startHighlightTimeout();  // Start the auto-dismissal timer
    static void cancelHighlightTimeout(); // Cancel the auto-dismissal timer early; already dismissed

    static Tile *highlightTarget; // Which tile are we highlighting? (Intending to highlight?)
    static bool highlightShown;   // Is the tile highlighted yet? Controlls highlight vs dismiss

  protected:
    int16_t left;
    int16_t top;
    uint16_t width;
    uint16_t height;

    Applet *assignedApplet = nullptr; // Pointer to the applet which is currently linked with the tile

    WindowManager *windowManager; // Convenient access to the WindowManager singleton
};

} // namespace NicheGraphics::InkHUD

#endif