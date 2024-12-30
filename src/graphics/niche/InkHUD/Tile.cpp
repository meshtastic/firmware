#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Tile.h"

#include "concurrency/Periodic.h"

using namespace NicheGraphics;

// For dismissing the highlight indicator, after a few seconds
// Highlighting is used to inform user of which tile is now focused
static concurrency::Periodic *taskDismissHighlight;
int32_t InkHUD::Tile::dismissHighlight()
{
    InkHUD::WindowManager::getInstance()->requestUpdate(Drivers::EInk::UpdateTypes::FAST, true);
    return taskDismissHighlight->disable();
}

InkHUD::Tile::Tile()
{
    // For convenince
    windowManager = InkHUD::WindowManager::getInstance();

    // Instantiate (once only) the task used to schedule the dismissal of tile highlighting
    static bool highlightSetupDone = false;
    if (!highlightSetupDone) {
        taskDismissHighlight = new concurrency::Periodic("Highlight", Tile::dismissHighlight);
        taskDismissHighlight->disable();
        highlightSetupDone = true;
    }
}

// Set the region of the tile automatically, based on the user's chosen layout
// This method places tiles which will host user applets
// The WindowManager multiplexes the applets to these tiles automatically
void InkHUD::Tile::placeUserTile(uint8_t userTileCount, uint8_t tileIndex)
{
    uint16_t displayWidth = windowManager->getWidth();
    uint16_t displayHeight = windowManager->getHeight();

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

    // Todo: special handling for the notification area
    // Todo: special handling for 3 tile layout

    // Gap between tiles
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

    this->left = left;
    this->top = top;
    this->width = width;
    this->height = height;
}

// Manually set the region for a tile
// This is only done for tiles which will host certain "System Applets", which have unique position / sizes:
// Things like the NotificationApplet, BatteryIconApplet, etc
void InkHUD::Tile::placeSystemTile(int16_t left, int16_t top, uint16_t width, uint16_t height)
{
    assert(width > 0 && height > 0);

    this->left = left;
    this->top = top;
    this->width = width;
    this->height = height;
}

// Render whichever applet is currently assigned to the tile
// The setTile call in responsible for informing at applet of its dimensions
// This is done here immediately prior to rendering
void InkHUD::Tile::render()
{
    // Render the tile's applet
    // ------------------------

    displayedApplet->setTile(this);
    displayedApplet->resetDrawingSpace();
    displayedApplet->render();

    // Highlighting:
    // identify which tile is focused when focus changes by aux button
    // --------------

    // Clear any old highlighting
    // Occurs at very next organic render of tile, or automatically after several seconds
    if (highlightShowing) {
        highlightShowing = false;
        taskDismissHighlight->disable();
    }

    // New highlighting, if requested
    if (highlightPending) {
        // Mark that a highlight is shown
        highlightPending = false;
        highlightShowing = true;

        // Drawing shouldn't really take place outside the Applet class itself,
        // But we're making an exception in this case
        displayedApplet->drawRect(0, 0, width, height, BLACK);

        // Schedule another refresh a few seconds from now, to remove the highlighting
        taskDismissHighlight->setIntervalFromNow(5 * 1000UL);
        taskDismissHighlight->enabled = true;
    }
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
        // Pass to the window manager
        windowManager->handleTilePixel(x, y, c);
    }
}

// Called by Applet base class, when learning of its dimensions
uint16_t InkHUD::Tile::getWidth()
{
    return width;
}

// Called by Applet base class, when learning of its dimensions
uint16_t InkHUD::Tile::getHeight()
{
    return height;
}

// (At next render) draw a border around the tile to indicate which applet is focused
// Used with WindowManager::nextApplet, which is sometimes assigned to the aux button
void InkHUD::Tile::highlight()
{
    highlightPending = true;
}

#endif