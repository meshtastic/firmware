#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Renderer.h"

#include "main.h"

#include "./Applet.h"
#include "./SystemApplet.h"
#include "./Tile.h"

using namespace NicheGraphics;

InkHUD::Renderer::Renderer() : concurrency::OSThread("Renderer")
{
    // Nothing for the timer to do just yet
    OSThread::disable();

    // Convenient references
    inkhud = InkHUD::getInstance();
    settings = &inkhud->persistence->settings;
}

// Connect the (fully set-up) E-Ink driver to InkHUD
// Should happen in your variant's nicheGraphics.h file, before InkHUD::begin is called
void InkHUD::Renderer::setDriver(Drivers::EInk *driver)
{
    // Make sure not already set
    if (this->driver) {
        LOG_ERROR("Driver already set");
        delay(2000); // Wait for native serial..
        assert(false);
    }

    // Store the driver which was created in setupNicheGraphics()
    this->driver = driver;

    // Determine the dimensions of the image buffer, in bytes.
    // Along rows, pixels are stored 8 per byte.
    // Not all display widths are divisible by 8. Need to make sure bytecount accommodates padding for these.
    imageBufferWidth = ((driver->width - 1) / 8) + 1;
    imageBufferHeight = driver->height;

    // Allocate the image buffer
    imageBuffer = new uint8_t[imageBufferWidth * imageBufferHeight];
}

// Set the target number of FAST display updates in a row, before a FULL update is used for display health
// This value applies only to updates with an UNSPECIFIED update type
// If explicitly requested FAST updates exceed this target, the stressMultiplier parameter determines how many
// subsequent FULL updates will be performed, in an attempt to restore the display's health
void InkHUD::Renderer::setDisplayResilience(uint8_t fastPerFull, float stressMultiplier)
{
    displayHealth.fastPerFull = fastPerFull;
    displayHealth.stressMultiplier = stressMultiplier;
}

void InkHUD::Renderer::begin()
{
    forceUpdate(Drivers::EInk::UpdateTypes::FULL, false);
}

// Set a flag, which will be picked up by runOnce, ASAP.
// Quite likely, multiple applets will all want to respond to one event (Observable, etc)
// Each affected applet can independently call requestUpdate(), and all share the one opportunity to render, at next runOnce
void InkHUD::Renderer::requestUpdate()
{
    requested = true;

    // We will run the thread as soon as we loop(),
    // after all Applets have had a chance to observe whatever event set this off
    OSThread::setIntervalFromNow(0);
    OSThread::enabled = true;
    runASAP = true;
}

// requestUpdate will not actually update if no requests were made by applets which are actually visible
// This can occur, because applets requestUpdate even from the background,
// in case the user's autoshow settings permit them to be moved to foreground.
// Sometimes, however, we will want to trigger a display update manually, in the absence of any sort of applet event
// Display health, for example.
// In these situations, we use forceUpdate
void InkHUD::Renderer::forceUpdate(Drivers::EInk::UpdateTypes type, bool async)
{
    requested = true;
    forced = true;
    displayHealth.forceUpdateType(type);

    // Normally, we need to start the timer, in case the display is busy and we briefly defer the update
    if (async) {
        // We will run the thread as soon as we loop(),
        // after all Applets have had a chance to observe whatever event set this off
        OSThread::setIntervalFromNow(0);
        OSThread::enabled = true;
        runASAP = true;
    }

    // If the update is *not* asynchronous, we begin the render process directly here
    // so that it can block code flow while running
    else
        render(false);
}

// Wait for any in-progress display update to complete before continuing
void InkHUD::Renderer::awaitUpdate()
{
    if (driver->busy()) {
        LOG_INFO("Waiting for display");
        driver->await(); // Wait here for update to complete
    }
}

// Set a ready-to-draw pixel into the image buffer
// All rotations / translations have already taken place: this buffer data is formatted ready for the driver
void InkHUD::Renderer::handlePixel(int16_t x, int16_t y, Color c)
{
    rotatePixelCoords(&x, &y);

    uint32_t byteNum = (y * imageBufferWidth) + (x / 8); // X data is 8 pixels per byte
    uint8_t bitNum = 7 - (x % 8); // Invert order: leftmost bit (most significant) is leftmost pixel of byte.

    bitWrite(imageBuffer[byteNum], bitNum, c);
}

// Width of the display, relative to rotation
uint16_t InkHUD::Renderer::width()
{
    if (settings->rotation % 2)
        return driver->height;
    else
        return driver->width;
}

// Height of the display, relative to rotation
uint16_t InkHUD::Renderer::height()
{
    if (settings->rotation % 2)
        return driver->width;
    else
        return driver->height;
}

// Runs at regular intervals
// - postponing render: until next loop(), allowing all applets to be notified of some Mesh event before render
// - queuing another render: while one is already is progress
int32_t InkHUD::Renderer::runOnce()
{
    // If an applet asked to render, and hardware is able, lets try now
    if (requested && !driver->busy()) {
        render();
    }

    // If our render() call failed, try again shortly
    // otherwise, stop our thread until next update due
    if (requested)
        return 250UL;
    else
        return OSThread::disable();
}

// Applies the system-wide rotation to pixel positions
// This step is applied to image data which has already been translated by a Tile object
// This is the final step before the pixel is placed into the image buffer
// No return: values of the *x and *y parameters are modified by the method
void InkHUD::Renderer::rotatePixelCoords(int16_t *x, int16_t *y)
{
    // Apply a global rotation to pixel locations
    int16_t x1 = 0;
    int16_t y1 = 0;
    switch (settings->rotation) {
    case 0:
        x1 = *x;
        y1 = *y;
        break;
    case 1:
        x1 = (driver->width - 1) - *y;
        y1 = *x;
        break;
    case 2:
        x1 = (driver->width - 1) - *x;
        y1 = (driver->height - 1) - *y;
        break;
    case 3:
        x1 = *y;
        y1 = (driver->height - 1) - *x;
        break;
    }
    *x = x1;
    *y = y1;
}

// Make an attempt to gather image data from some / all applets, and update the display
// Might not be possible right now, if update already is progress.
void InkHUD::Renderer::render(bool async)
{
    // Make sure the display is ready for a new update
    if (async) {
        // Previous update still running, Will try again shortly, via runOnce()
        if (driver->busy())
            return;
    } else {
        // Wait here for previous update to complete
        driver->await();
    }

    // Determine if a system applet has requested exclusive rights to request an update,
    // or exclusive rights to render
    checkLocks();

    // (Potentially) change applet to display new info,
    // then check if this newly displayed applet makes a pending notification redundant
    inkhud->autoshow();

    // If an update is justified.
    // We don't know this until after autoshow has run, as new applets may now be in foreground
    if (shouldUpdate()) {

        // Decide which technique the display will use to change image
        // Done early, as rendering resets the Applets' requested types
        Drivers::EInk::UpdateTypes updateType = decideUpdateType();

        // Render the new image
        clearBuffer();
        renderUserApplets();
        renderPlaceholders();
        renderSystemApplets();

        // Invert Buffer if set by user
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
            for (size_t i = 0; i < imageBufferWidth * imageBufferHeight; ++i) {
                imageBuffer[i] = ~imageBuffer[i];
            }
        }

        // Tell display to begin process of drawing new image
        LOG_INFO("Updating display");
        driver->update(imageBuffer, updateType);

        // If not async, wait here until the update is complete
        if (!async)
            driver->await();
    }

    // Our part is done now.
    // If update is async, the display hardware is still performing the update process,
    // but that's all handled by NicheGraphics::Drivers::EInk

    // Tidy up, ready for a new request
    requested = false;
    forced = false;
}

// Manually fill the image buffer with WHITE
// Clears any old drawing
// Note: benchmarking revealed that this is *much* faster than setting pixels individually
// So much so that it's more efficient to re-render all applets,
// rather than rendering selectively, and manually blanking a portion of the display
void InkHUD::Renderer::clearBuffer()
{
    memset(imageBuffer, 0xFF, imageBufferHeight * imageBufferWidth);
}

void InkHUD::Renderer::checkLocks()
{
    lockRendering = nullptr;
    lockRequests = nullptr;

    for (SystemApplet *sa : inkhud->systemApplets) {
        if (!lockRendering && sa->lockRendering && sa->isForeground()) {
            lockRendering = sa;
        }
        if (!lockRequests && sa->lockRequests && sa->isForeground()) {
            lockRequests = sa;
        }
    }
}

bool InkHUD::Renderer::shouldUpdate()
{
    bool should = false;

    // via forceUpdate
    should |= forced;

    // via a system applet (which has locked update requests)
    if (lockRequests) {
        should |= lockRequests->wantsToRender();
        return should; // Early exit - no other requests considered
    }

    // via system applet (not locked)
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->wantsToRender()    // This applet requested
            && sa->isForeground()) // This applet is currently shown
        {
            should = true;
            break;
        }
    }

    // via user applet
    for (Applet *ua : inkhud->userApplets) {
        if (ua                     // Tile has valid applet
            && ua->wantsToRender() // This applet requested display update
            && ua->isForeground()) // This applet is currently shown
        {
            should = true;
            break;
        }
    }

    return should;
}

// Determine which type of E-Ink update the display will perform, to change the image.
// Considers the needs of the various applets, then weighs against display health.
// An update type specified by forceUpdate will be granted with no further questioning.
Drivers::EInk::UpdateTypes InkHUD::Renderer::decideUpdateType()
{
    // Ask applets which update type they would prefer
    // Some update types take priority over others

    // No need to consider the "requests" if somebody already forced an update
    if (!forced) {
        // User applets
        for (Applet *ua : inkhud->userApplets) {
            if (ua && ua->isForeground())
                displayHealth.requestUpdateType(ua->wantsUpdateType());
        }
        // System Applets
        for (SystemApplet *sa : inkhud->systemApplets) {
            if (sa && sa->isForeground())
                displayHealth.requestUpdateType(sa->wantsUpdateType());
        }
    }

    return displayHealth.decideUpdateType();
}

// Run the drawing operations of any user applets which are currently displayed
// Pixel output is placed into the framebuffer, ready for handoff to the EInk driver
void InkHUD::Renderer::renderUserApplets()
{
    // Don't render user applets if a system applet has demanded the whole display to itself
    if (lockRendering)
        return;

    // Render any user applets which are currently visible
    for (Applet *ua : inkhud->userApplets) {
        if (ua && ua->isActive() && ua->isForeground()) {
            uint32_t start = millis();
            ua->render(); // Draw!
            uint32_t stop = millis();
            LOG_DEBUG("%s took %dms to render", ua->name, stop - start);
        }
    }
}

// Run the drawing operations of any system applets which are currently displayed
// Pixel output is placed into the framebuffer, ready for handoff to the EInk driver
void InkHUD::Renderer::renderSystemApplets()
{
    SystemApplet *battery = inkhud->getSystemApplet("BatteryIcon");
    SystemApplet *menu = inkhud->getSystemApplet("Menu");
    SystemApplet *notifications = inkhud->getSystemApplet("Notification");

    // Each system applet
    for (SystemApplet *sa : inkhud->systemApplets) {

        // Skip if not shown
        if (!sa->isForeground())
            continue;

        // Skip if locked by another applet
        if (lockRendering && lockRendering != sa)
            continue;

        // Don't draw the battery or notifications overtop the menu
        // Todo: smarter way to handle this
        if (menu->isForeground() && (sa == battery || sa == notifications))
            continue;

        assert(sa->getTile());

        // uint32_t start = millis();
        sa->render(); // Draw!
        // uint32_t stop = millis();
        // LOG_DEBUG("%s took %dms to render", sa->name, stop - start);
    }
}

// In some situations (e.g. layout or applet selection changes),
// a user tile can end up without an assigned applet.
// In this case, we will fill the empty space with diagonal lines.
void InkHUD::Renderer::renderPlaceholders()
{
    // Don't fill empty space with placeholders if a system applet wants exclusive use of the display
    if (lockRendering)
        return;

    // Ask the window manager which tiles are empty
    std::vector<Tile *> emptyTiles = inkhud->getEmptyTiles();

    // No empty tiles
    if (emptyTiles.size() == 0)
        return;

    SystemApplet *placeholder = inkhud->getSystemApplet("Placeholder");

    // uint32_t start = millis();
    for (Tile *t : emptyTiles) {
        t->assignApplet(placeholder);
        placeholder->render();
        t->assignApplet(nullptr);
    }
    // uint32_t stop = millis();
    // LOG_DEBUG("Placeholders took %dms to render", stop - start);
}

#endif