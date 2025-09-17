#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Tile.h"

#include "concurrency/Periodic.h"

using namespace NicheGraphics;

// Static members of Tile class (for linking)
InkHUD::Tile *InkHUD::Tile::highlightTarget;
bool InkHUD::Tile::highlightShown;

// For dismissing the highlight indicator, after a few seconds
// Highlighting is used to inform user of which tile is now focused
static concurrency::Periodic *taskHighlight;
static int32_t runtaskHighlight()
{
    LOG_DEBUG("Dismissing Highlight");
    InkHUD::Tile::highlightShown = false;
    InkHUD::Tile::highlightTarget = nullptr;
    InkHUD::InkHUD::getInstance()->forceUpdate(Drivers::EInk::UpdateTypes::FAST); // Re-render, clearing the highlighting
    return taskHighlight->disable();
}
static void inittaskHighlight()
{
    static bool doneOnce = false;
    if (!doneOnce) {
        taskHighlight = new concurrency::Periodic("Highlight", runtaskHighlight);
        taskHighlight->disable();
        doneOnce = true;
    }
}

InkHUD::Tile::Tile()
{
    inkhud = InkHUD::getInstance();

    inittaskHighlight();
    Tile::highlightTarget = nullptr;
    Tile::highlightShown = false;
}

InkHUD::Tile::Tile(int16_t left, int16_t top, uint16_t width, uint16_t height)
{
    assert(width > 0 && height > 0);

    this->left = left;
    this->top = top;
    this->width = width;
    this->height = height;
}

// Set the region of the tile automatically, based on the user's chosen layout
// This method places tiles which will host user applets
// The WindowManager multiplexes the applets to these tiles automatically
void InkHUD::Tile::setRegion(uint8_t userTileCount, uint8_t tileIndex)
{
    uint16_t displayWidth = inkhud->width();
    uint16_t displayHeight = inkhud->height();

    bool landscape = displayWidth > displayHeight;

    // Check for any stray tiles
    if (tileIndex > (userTileCount - 1)) {
        // Dummy values to prevent rendering
        LOG_WARN("Tile index out of bounds");
        left = -2;
        top = -2;
        width = 1;
        height = 1;
        return;
    }

    // Todo: special handling for 3 tile layout

    // Gutters between tiles
    const uint16_t spacing = 4;

    switch (userTileCount) {
    // One tile only
    case 1:
        left = 0;
        top = 0;
        width = displayWidth;
        height = displayHeight;
        break;

    // Two tiles
    case 2:
        if (landscape) {
            // Side by side
            left = ((displayWidth / 2) + (spacing / 2)) * tileIndex;
            top = 0;
            width = (displayWidth / 2) - (spacing / 2);
            height = displayHeight;
        } else {
            // Above and below
            left = 0;
            top = 0 + (((displayHeight / 2) + (spacing / 2)) * tileIndex);
            width = displayWidth;
            height = (displayHeight / 2) - (spacing / 2);
        }
        break;

    // Four tiles
    case 4:
        width = (displayWidth / 2) - (spacing / 2);
        height = (displayHeight / 2) - (spacing / 2);
        switch (tileIndex) {
        case 0:
            left = 0;
            top = 0;
            break;
        case 1:
            left = 0 + (width - 1) + spacing;
            top = 0;
            break;
        case 2:
            left = 0;
            top = 0 + (height - 1) + spacing;
            break;
        case 3:
            left = 0 + (width - 1) + spacing;
            top = 0 + (height - 1) + spacing;
            break;
        }
        break;

    default:
        LOG_ERROR("Unsupported tile layout");
        assert(0);
    }

    assert(width > 0 && height > 0);
}

// Manually set the region for a tile
// This is only done for tiles which will host certain "System Applets", which have unique position / sizes:
// Things like the NotificationApplet, BatteryIconApplet, etc
void InkHUD::Tile::setRegion(int16_t left, int16_t top, uint16_t width, uint16_t height)
{
    assert(width > 0 && height > 0);

    this->left = left;
    this->top = top;
    this->width = width;
    this->height = height;
}

// Place an applet onto a tile
// Creates a reciprocal link between applet and tile
// The tile should always know which applet is displayed
// The applet should always know which tile it is display on
// This is enforced with asserts
// Assigning a new applet will break a previous link
// Link may also be broken by assigning a nullptr
void InkHUD::Tile::assignApplet(Applet *a)
{
    // Break the link between old applet and this tile
    if (assignedApplet)
        assignedApplet->setTile(nullptr);

    // Store the new applet
    assignedApplet = a;

    // Create the reciprocal link between the new applet and this tile
    if (a)
        a->setTile(this);
}

// Get pointer to whichever applet is displayed on this tile
InkHUD::Applet *InkHUD::Tile::getAssignedApplet()
{
    return assignedApplet;
}

// Receive drawing output from the assigned applet,
// and translate it from "applet-space" coordinates, to it's true location.
// The final "rotation" step is performed by the windowManager
void InkHUD::Tile::handleAppletPixel(int16_t x, int16_t y, Color c)
{
    // Move pixels from applet-space to tile-space
    x += left;
    y += top;

    // Crop to tile borders
    if (x >= left && x < (left + width) && y >= top && y < (top + height)) {
        // Pass to the renderer
        inkhud->drawPixel(x, y, c);
    }
}

// Called by Applet base class, when setting applet dimensions, immediately before render
uint16_t InkHUD::Tile::getWidth()
{
    return width;
}

// Called by Applet base class, when setting applet dimensions, immediately before render
uint16_t InkHUD::Tile::getHeight()
{
    return height;
}

// Longest edge of the display, in pixels
// A 296px x 250px display will return 296, for example
// Maximum possible size of any tile's width / height
// Used by some components to allocate resources for the "worst possible situation"
// "Sizing the cathedral for christmas eve"
uint16_t InkHUD::Tile::maxDisplayDimension()
{
    InkHUD *inkhud = InkHUD::getInstance();
    return max(inkhud->height(), inkhud->width());
}

// Ask for this tile to be highlighted
// Used to indicate which tile is now indicated after focus changes
// Only used for aux button focus changes, not changes via menu
void InkHUD::Tile::requestHighlight()
{
    Tile::highlightTarget = this;
    Tile::highlightShown = false;
    inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Starts the timer which will automatically dismiss the highlighting, if the tile doesn't organically redraw first
void InkHUD::Tile::startHighlightTimeout()
{
    taskHighlight->setIntervalFromNow(5 * 1000UL);
    taskHighlight->enabled = true;
}

// Stop the timer which would automatically dismiss the highlighting
// Called if the tile organically renders before the timer is up
void InkHUD::Tile::cancelHighlightTimeout()
{
    if (taskHighlight->enabled)
        taskHighlight->disable();
}

#endif