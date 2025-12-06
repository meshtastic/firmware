#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./InkHUD.h"

#include "./Applet.h"
#include "./Events.h"
#include "./Persistence.h"
#include "./Renderer.h"
#include "./SystemApplet.h"
#include "./Tile.h"
#include "./WindowManager.h"

using namespace NicheGraphics;

// Get or create the singleton
InkHUD::InkHUD *InkHUD::InkHUD::getInstance()
{
    // Create the singleton instance of our class, if not yet done
    static InkHUD *instance = nullptr;
    if (!instance) {
        instance = new InkHUD;

        instance->persistence = new Persistence;
        instance->windowManager = new WindowManager;
        instance->renderer = new Renderer;
        instance->events = new Events;
    }

    return instance;
}

// Connect the (fully set-up) E-Ink driver to InkHUD
// Should happen in your variant's nicheGraphics.h file, before InkHUD::begin is called
void InkHUD::InkHUD::setDriver(Drivers::EInk *driver)
{
    renderer->setDriver(driver);
}

// Set the target number of FAST display updates in a row, before a FULL update is used for display health
// This value applies only to updates with an UNSPECIFIED update type
// If explicitly requested FAST updates exceed this target, the stressMultiplier parameter determines how many
// subsequent FULL updates will be performed, in an attempt to restore the display's health
void InkHUD::InkHUD::setDisplayResilience(uint8_t fastPerFull, float stressMultiplier)
{
    renderer->setDisplayResilience(fastPerFull, stressMultiplier);
}

// Register a user applet with InkHUD
// A variant's nicheGraphics.h file should instantiate your chosen applets, then pass them to this method
// Passing an applet to this method is all that is required to make it available to the user in your InkHUD build
void InkHUD::InkHUD::addApplet(const char *name, Applet *a, bool defaultActive, bool defaultAutoshow, uint8_t onTile)
{
    windowManager->addApplet(name, a, defaultActive, defaultAutoshow, onTile);
}

// Start InkHUD!
// Call this only after you have configured InkHUD
void InkHUD::InkHUD::begin()
{
    persistence->loadSettings();
    persistence->loadLatestMessage();

    windowManager->begin();
    events->begin();
    renderer->begin();
    // LogoApplet shows boot screen here
}

// Call this when your user button gets a short press
// Should be connected to an input source in nicheGraphics.h (NicheGraphics::Inputs::TwoButton?)
void InkHUD::InkHUD::shortpress()
{
    events->onButtonShort();
}

// Call this when your user button gets a long press
// Should be connected to an input source in nicheGraphics.h (NicheGraphics::Inputs::TwoButton?)
void InkHUD::InkHUD::longpress()
{
    events->onButtonLong();
}

// Cycle the next user applet to the foreground
// Only activated applets are cycled
// If user has a multi-applet layout, the applets will cycle on the "focused tile"
void InkHUD::InkHUD::nextApplet()
{
    windowManager->nextApplet();
}

// Show the menu (on the the focused tile)
// The applet previously displayed there will be restored once the menu closes
void InkHUD::InkHUD::openMenu()
{
    windowManager->openMenu();
}

// In layouts where multiple applets are shown at once, change which tile is focused
// The focused tile in the one which cycles applets on button short press, and displays menu on long press
void InkHUD::InkHUD::nextTile()
{
    windowManager->nextTile();
}

// Rotate the display image by 90 degrees
void InkHUD::InkHUD::rotate()
{
    windowManager->rotate();
}

// Show / hide the battery indicator in top-right
void InkHUD::InkHUD::toggleBatteryIcon()
{
    windowManager->toggleBatteryIcon();
}

// An applet asking for the display to be updated
// This does not occur immediately
// Instead, rendering is scheduled ASAP, for the next Renderer::runOnce call
// This allows multiple applets to observe the same event, and then share the same opportunity to update
// Applets should requestUpdate, whether or not they are currently displayed ("foreground")
// This is because they *might* be automatically brought to foreground by WindowManager::autoshow
void InkHUD::InkHUD::requestUpdate()
{
    renderer->requestUpdate();
}

// Demand that the display be updated
// Ignores all diplomacy:
//  - the display *will* update
//  - the specified update type *will* be used
// If the async parameter is false, code flow is blocked while the update takes place
void InkHUD::InkHUD::forceUpdate(EInk::UpdateTypes type, bool async)
{
    renderer->forceUpdate(type, async);
}

// Wait for any in-progress display update to complete before continuing
void InkHUD::InkHUD::awaitUpdate()
{
    renderer->awaitUpdate();
}

// Ask the window manager to potentially bring a different user applet to foreground
// An applet will be brought to foreground if it has just received new and relevant info
// For Example: AllMessagesApplet has just received a new text message
// Permission for this autoshow behavior is granted by the user, on an applet-by-applet basis
// If autoshow brings an applet to foreground, an InkHUD notification will not be generated for the same event
void InkHUD::InkHUD::autoshow()
{
    windowManager->autoshow();
}

// Tell the window manager that the Persistence::Settings value for applet activation has changed,
// and that it should reconfigure accordingly.
// This is triggered at boot, or when the user enables / disabled applets via the on-screen menu
void InkHUD::InkHUD::updateAppletSelection()
{
    windowManager->changeActivatedApplets();
}

// Tell the window manager that the Persistence::Settings value for layout or rotation has changed,
// and that it should reconfigure accordingly.
// This is triggered at boot, or by rotate / layout options in the on-screen menu
void InkHUD::InkHUD::updateLayout()
{
    windowManager->changeLayout();
}

// Width of the display, in the context of the current rotation
uint16_t InkHUD::InkHUD::width()
{
    return renderer->width();
}

// Height of the display, in the context of the current rotation
uint16_t InkHUD::InkHUD::height()
{
    return renderer->height();
}

// A collection of any user tiles which do not have a valid user applet
// This can occur in various situations, such as when a user enables fewer applets than their layout has tiles
// The tiles (and which regions the occupy) are private information of the window manager
// The renderer needs to know which regions (if any) are empty,
// in order to fill them with a "placeholder" pattern.
// -- There may be a tidier way to accomplish this --
std::vector<InkHUD::Tile *> InkHUD::InkHUD::getEmptyTiles()
{
    return windowManager->getEmptyTiles();
}

// Get a system applet by its name
// This isn't particularly elegant, but it does avoid:
// - passing around a big set of references
// - having two sets of references (systemApplet vector for iteration)
InkHUD::SystemApplet *InkHUD::InkHUD::getSystemApplet(const char *name)
{
    for (SystemApplet *sa : systemApplets) {
        if (strcmp(name, sa->name) == 0)
            return sa;
    }

    assert(false); // Invalid name
}

// Place a pixel into the image buffer
// The x and y coordinates are in the context of the current display rotation
// - Applets pass "relative" pixels to tiles
// - Tiles pass translated pixels to this method
// - this methods (Renderer) places rotated pixels into the image buffer
// This method provides the final formatting step required. The image buffer is suitable for writing to display
void InkHUD::InkHUD::drawPixel(int16_t x, int16_t y, Color c)
{
    renderer->handlePixel(x, y, c);
}

#endif