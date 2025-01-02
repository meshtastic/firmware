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
    Applet *assignedApplet = nullptr; // Pointer to the applet which is currently displayed on the tile

  protected:
    int16_t left;
    int16_t top;
    uint16_t width;
    uint16_t height;

    WindowManager *windowManager; // Convenient access to the WindowManager singleton

    bool highlightPending = false; // Asking to highlight focused tile with indicator at next render
    bool highlightShowing = false; // Focused tile is currently highlighted on display with an indicator
};

} // namespace NicheGraphics::InkHUD

#endif