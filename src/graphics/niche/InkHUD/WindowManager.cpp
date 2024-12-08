#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./WindowManager.h"

#include "mesh/NodeDB.h"
#include "rtc.h"

// System applets
// Must be defined in .cpp to prevent a circular dependency with Applet base class
#include "./Applets/System/BatteryIcon/BatteryIconApplet.h"
#include "./Applets/System/BootScreen/BootScreenApplet.h"
#include "./Applets/System/Menu/MenuApplet.h"
#include "./Applets/System/Notification/NotificationApplet.h"
#include "./Applets/System/Placeholder/PlaceholderApplet.h"

using namespace NicheGraphics;

InkHUD::WindowManager::WindowManager() : concurrency::OSThread("InkHUD WM")
{
    // Nothing for the timer to do just yet
    OSThread::disable();
}

// Get or create the WindowManager singleton
InkHUD::WindowManager *InkHUD::WindowManager::getInstance()
{
    // Create the singleton instance of our class, if not yet done
    static InkHUD::WindowManager *instance = new InkHUD::WindowManager();
    return instance;
}

// Connect the driver, which is created independently is setupNicheGraphics()
void InkHUD::WindowManager::setDriver(Drivers::EInk *driver)
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

    // Clear the display right now
    clearBuffer();
    driver->update(imageBuffer, Drivers::EInk::FULL);
}

// Register a user applet with the WindowManager
// This is called in setupNicheGraphics()
// This should be the only time that specific user applets are mentioned in the code
// If a user applet is not added with this method, its code should not be built
void InkHUD::WindowManager::addApplet(const char *name, Applet *a, bool defaultActive, bool defaultAutoshow)
{
    userApplets.push_back(a);

    // If requested, mark in settings that this applet should be active by default
    // This means that it will be available for the user to cycle to with short-press of the button
    // This is the default state only: user can activate or deactive applets through the menu.
    // User's choice of active applets is stored in settings, and will be honored instead of these defaults, if present
    if (defaultActive)
        settings.userApplets.active[userApplets.size() - 1] = true;

    // If requested, mark in settings that this applet should "autoshow" by default
    // This means that the applet will be automatically brought to foreground when it has new data to show
    // This is the default state only: user can select which applets have this behavior through the menu
    // User's selection is stored in settings, and will be honored instead of these defaults, if present
    if (defaultAutoshow)
        settings.userApplets.autoshow[userApplets.size() - 1] = true;

    // The label that will be show in the applet selection menu, on the device
    a->name = name;
}

// Perform initial setup, and begin responding to incoming events
// First task once init is to show the boot screen
void InkHUD::WindowManager::begin()
{
    // Make sure we have set a driver
    if (!this->driver) {
        LOG_ERROR("Driver not set");
        delay(2000); // Wait for native serial..
        assert(false);
    }

    loadSettingsFromFlash();

    createSystemApplets();
    createSystemTiles();
    placeSystemTiles();
    assignSystemAppletsToTiles();

    createUserApplets();
    createUserTiles();
    placeUserTiles();
    assignUserAppletsToTiles();
    refocusTile();

    // menuApplet is assigned to the focused user tile when opened

    bootscreenApplet->bringToForeground();

    deepSleepObserver.observe(&notifyDeepSleep);
    textMessageObserver.observe(textMessageModule);
}

// Set-up special "system applets"
// These handle things like bootscreen, pop-up notifications etc
// They are processed separately from the user applets, because they might need to do "weird things"
// They also won't be activated or deactivated
void InkHUD::WindowManager::createSystemApplets()
{
    bootscreenApplet = new BootScreenApplet;
    notificationApplet = new NotificationApplet;
    batteryIconApplet = new BatteryIconApplet;
    menuApplet = new MenuApplet;
    placeholderApplet = new PlaceholderApplet;

    // System applets are always active
    bootscreenApplet->activate();
    // notificationApplet->activate();
    batteryIconApplet->activate();
    menuApplet->activate();
    placeholderApplet->activate();

    systemApplets.push_back(bootscreenApplet);
    systemApplets.push_back(notificationApplet);
}

void InkHUD::WindowManager::createSystemTiles()
{
    fullscreenTile = new Tile;
    notificationTile = new Tile;
    batteryIconTile = new Tile;
}

void InkHUD::WindowManager::placeSystemTiles()
{
    fullscreenTile->placeSystemTile(0, 0, getWidth(), getHeight());
    notificationTile->placeSystemTile(0, 0, getWidth(), 20); // Testing only: constant value

    // Todo: appropriate sizing for the battery icon
    const uint16_t batteryIconHeight = Applet::getHeaderHeight() - (2 * 2);
    uint16_t batteryIconWidth = batteryIconHeight * 1.8;

    batteryIconTile->placeSystemTile(getWidth() - batteryIconWidth, 2, batteryIconWidth, batteryIconHeight);
}

void InkHUD::WindowManager::assignSystemAppletsToTiles()
{
    // Assign tiles
    bootscreenApplet->setTile(fullscreenTile);
    notificationApplet->setTile(notificationTile);
    batteryIconApplet->setTile(batteryIconTile);
}

// Activate or deactivate user applets, to match settings
// Called at boot, or after run-time config changes via menu
// Note: this method does not instantiate the applets;
// this is done in setupNicheGraphics, with WindowManager::addApplet
void InkHUD::WindowManager::createUserApplets()
{
    // Deactivate and remove any no-longer-needed applets
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        Applet *a = userApplets.at(i);

        // If the applet is active, but settings say it shouldn't be:
        // - run applet's custom deactivation code
        // - mark applet as inactive (internally)
        if (a->isActive() && !settings.userApplets.active[i])
            a->deactivate();
    }

    // Activate and add any new applets
    for (uint8_t i = 0; i < userApplets.size() && i < MAX_USERAPPLETS_GLOBAL; i++) {

        // If not activated, but it now should be:
        // - run applet's custom activation code
        // - mark applet as active (internally)
        if (!userApplets.at(i)->isActive() && settings.userApplets.active[i])
            userApplets.at(i)->activate();
    }
}

void InkHUD::WindowManager::createUserTiles()
{
    // Delete any tiles which currently exist
    for (Tile *t : userTiles)
        delete t;
    userTiles.clear();

    // Create new tiles
    for (uint8_t i = 0; i < settings.userTiles.count; i++) {
        Tile *t = new Tile;
        userTiles.push_back(t);
    }
}

void InkHUD::WindowManager::placeUserTiles()
{
    // Calculate the display region occupied by each tile
    // This determines how pixels are translated from applet-space to windowmanager-space
    for (uint8_t i = 0; i < userTiles.size(); i++)
        userTiles.at(i)->placeUserTile(settings.userTiles.count, i);
}

void InkHUD::WindowManager::assignUserAppletsToTiles()
{
    // Set "displayedApplet" property
    // Which applet should be initially shown on a tile?
    // This is preserved between reboots, but the value needs validating at startup
    for (uint8_t i = 0; i < userTiles.size(); i++) {
        Tile *t = userTiles.at(i);

        // Check whether tile can display the previously shown applet again
        uint8_t oldIndex = settings.userTiles.displayedUserApplet[i]; // Previous index in WindowManager::userApplets
        bool canRestore = true;
        if (oldIndex > userApplets.size() - 1) // Check if old index is now out of bounds
            canRestore = false;
        else if (!settings.userApplets.active[oldIndex]) // Check that old applet is still activated
            canRestore = false;
        else { // Check that the old applet isn't now shown already on a different tile
            for (uint8_t i2 = 0; i2 < i; i2++) {
                if (settings.userTiles.displayedUserApplet[i2] == oldIndex) {
                    canRestore = false;
                    break;
                }
            }
        }

        // Restore previously shown applet if possible,
        // otherwise show placeholder
        if (canRestore) {
            t->displayedApplet = userApplets.at(oldIndex);
            t->displayedApplet->bringToForeground();
        } else {
            t->displayedApplet = placeholderApplet;
            settings.userTiles.displayedUserApplet[i] = -1; // Update settings: current tile has no valid applet
        }
    }
}

void InkHUD::WindowManager::refocusTile()
{
    // Validate "focused tile" setting
    // - info: focused tile responds to button presses: applet cycling, menu, etc
    // - if number of tiles changed, might now be out of index
    if (settings.userTiles.focused >= userTiles.size())
        settings.userTiles.focused = 0;

    // Change "focused tile" from placeholder, to a valid applet
    // - scan for another valid applet, which we can addSubstitution
    // - reason: nextApplet() won't cycle if placeholder is shown
    Tile *focusedTile = userTiles.at(settings.userTiles.focused);
    if (focusedTile->displayedApplet == placeholderApplet) {
        // Search for available applets
        for (uint8_t i = 0; i < userApplets.size(); i++) {
            Applet *a = userApplets.at(i);
            if (a->isActive() && !a->isForeground()) {
                // Found a suitable applet
                // Assign it to the focused tile
                focusedTile->displayedApplet = a;
                a->bringToForeground();
                settings.userTiles.displayedUserApplet[settings.userTiles.focused] = i; // Record change: persist after reboot
                break;
            }
        }
    }
}

// Callback for deepSleepObserver
// Returns 0 to signal that we agree to sleep now
int InkHUD::WindowManager::beforeDeepSleep(void *unused)
{
    for (Applet *a : userApplets) {
        a->onDeactivate();
        a->onShutdown();
    }

    saveSettingsToFlash();
    return 0; // We agree: deep sleep now
}

// Callback when a new text message is received
// Caches the most recently received message, for use by applets
// Rx does not trigger a save to flash, however the data *will* be saved alongside other during shutdown, etc.
// Note: this is different from devicestate.rx_text_message, which may contain an *outgoing* message
int InkHUD::WindowManager::onReceiveTextMessage(const meshtastic_MeshPacket *packet)
{
    // If message is incoming, not outgoing
    if (getFrom(packet) != nodeDB->getNodeNum()) {

        // Copy data from the text message into our InkHUD settings
        strncpy(settings.lastMessage.text, (const char *)packet->decoded.payload.bytes, packet->decoded.payload.size);
        settings.lastMessage.text[packet->decoded.payload.size] = '\0'; // Append null term

        // Store nodenum of the sender
        // Applets can use this to fetch user data from nodedb, if they want
        settings.lastMessage.nodeNum = packet->from;

        // Store the time (epoch seconds) when message received
        settings.lastMessage.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time

        // Store the channel
        // - (potentially) used to determine whether notification shows
        // - (potentially) used to determine which applet to focus
        settings.lastMessage.channelIndex = packet->channel;
    }

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

// Triggered by an input source when a short-press fires
// The input source is a separate component; not part of InkHUD
// It is connected in setupNicheGraphics()
void InkHUD::WindowManager::handleButtonShort()
{
    if (notificationApplet->isForeground())
        notificationApplet->dismiss();

    else if (!menuApplet->isForeground())
        nextApplet();
    else
        menuApplet->onButtonShortPress();
}

// Triggered by an input source when a long-press fires
// The input source is a separate component; not part of InkHUD
// It is connected in setupNicheGraphics()
// Note: input source should raise this while button still held
void InkHUD::WindowManager::handleButtonLong()
{
    // Open the menu
    if (!menuApplet->isForeground()) {
        userTiles.at(settings.userTiles.focused)->displayedApplet->sendToBackground();
        menuApplet->setTile(userTiles.at(settings.userTiles.focused));
        menuApplet->bringToForeground(Drivers::EInk::UpdateTypes::FAST);
    }
    // Or let the menu handle it
    else
        menuApplet->onButtonLongPress();
}

// Some devices will have a secondary button
// In InkHUD, this has a configurable behavior
// This handler is raised by the (Non-InkHUD) input source as soon as the button moves down
// This is before a short click fires
// This is used to handle "indefinite hold" behavior, if required
void InkHUD::WindowManager::handleAuxButtonDown()
{
// Testing only
#ifdef TTGO_T_ECHO
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, HIGH);
#endif
}

// Some devices will have a secondary button
// In InkHUD, this has a configurable behavior
// This handler is raised by the (Non-InkHUD) input source as soon as the button moves up
// This will happen on every release, regardless of any short or less press which is also raised
// This is used to handle "indefinite hold" behavior, if required
void InkHUD::WindowManager::handleAuxButtonUp()
{
// Testing only
#ifdef TTGO_T_ECHO
    digitalWrite(PIN_EINK_EN, LOW);
#endif
}

// Some devices will have a secondary button
// In InkHUD, this has a configurable behavior
// This handler is raised by the (Non-InkHUD) input source when this secondary button is short pressed
void InkHUD::WindowManager::handleAuxButtonShort() {}

// Some devices will have a secondary button
// In InkHUD, this has a configurable behavior
// This handler is raised by the (Non-InkHUD) input source when this secondary button is long pressed
// Note: should fire while still held
void InkHUD::WindowManager::handleAuxButtonLong() {}

// On the currently focussed tile: cycle to the next available user applet
// Applets available for this must be activated, and not already displayed on another tile
void InkHUD::WindowManager::nextApplet()
{
    Tile *t = userTiles.at(settings.userTiles.focused);

    // Short circuit: zero applets available
    if (t->displayedApplet == placeholderApplet)
        return;

    // Find the index of the applet currently shown on the tile
    uint8_t appletIndex = -1;
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        if (userApplets.at(i) == t->displayedApplet) {
            appletIndex = i;
            break;
        }
    }

    // Confirm that we did find the applet
    assert(appletIndex != (uint8_t)-1);

    // Iterate forward through the WindowManager::applets, looking for the next valid applet
    Applet *nextValidApplet = nullptr;
    // for (uint8_t i = (appletIndex + 1) % applets.size(); i != appletIndex; i = (i + 1) % applets.size()) {
    for (uint8_t i = 1; i < userApplets.size(); i++) {
        uint8_t newAppletIndex = (appletIndex + i) % userApplets.size();
        Applet *a = userApplets.at(newAppletIndex);

        // Looking for an applet which is active (enabled by user), but currently in background
        if (a->isActive() && !a->isForeground()) {
            nextValidApplet = a;
            settings.userTiles.displayedUserApplet[settings.userTiles.focused] =
                newAppletIndex; // Remember this setting between boots!
            break;
        }
    }

    // Confirm that we found another applet
    if (!nextValidApplet)
        return;

    // Hide old applet, show new applet
    t->displayedApplet->sendToBackground();
    t->displayedApplet = nextValidApplet;
    t->displayedApplet->bringToForeground(Drivers::EInk::UpdateTypes::FAST);
    render(true);
}

// Focus on a different tile
// The "focused tile" is the one which cycles applets on user button press,
// and the one where the menu will be displayed
void InkHUD::WindowManager::nextTile()
{
    settings.userTiles.focused = (settings.userTiles.focused + 1) % userTiles.size();
    changeLayout();
}

// Perform necessary reconfiguration when user changes number of tiles (or rotation) at run-time
// Call after changing settings.tiles.count
void InkHUD::WindowManager::changeLayout()
{
    // Tile count or rotation can only change at run-time via the menu applet
    assert(menuApplet->isForeground());

    // Remove all old drawing
    // Prevents pixels getting stuck in space which forms the gap between applets
    clearBuffer();

    // Recreate tiles
    // - correct number created, from settings.userTiles.count
    // - set dimension and position of tiles, according to layout
    createUserTiles();
    placeUserTiles();
    placeSystemTiles();

    // Handle fewer tiles
    // - background any applets which have lost their tile
    findOrphanApplets();

    // Handle more tiles
    // - create extra applets
    // - assign them to the new extra tiles
    createUserApplets();
    assignUserAppletsToTiles();

    // Focus a valid tile
    // - info: focused tile is the one which cycles applets when user button pressed
    // - may now be out of bounds if tile count has decreased
    refocusTile();

    // Restore menu
    // - its tile was just destroyed and recreated (createUserTiles)
    // - its assignment was cleared (assignUserAppletsToTiles)
    if (menuApplet->isForeground())
        menuApplet->setTile(userTiles.at(settings.userTiles.focused));

    // Note: this "adhoc redraw" action which several methods use will be abstracted,
    // when WindowManager::render() gets refactored

    // Draw user applets (no refresh)
    // - recreates the "frozen" user applets, shown on tiles not showing the menu applet
    // - menu will draw over-top before next render()
    for (Tile *t : userTiles) {
        t->displayedApplet->setTile(t);          // Set dimensions; provide a reference to this tile, so it can receive pixels
        t->displayedApplet->resetDrawingSpace(); // Reset the drawing environment
        t->displayedApplet->render();            // Run the drawing operation, feeding pixels via Tile, into WindowManager
    }

    // If the battery icon is shown, ad-hoc redraw this too
    if (batteryIconApplet->isForeground()) {
        batteryIconApplet->setTile(batteryIconApplet->getTile());
        batteryIconApplet->resetDrawingSpace();
        batteryIconApplet->render();
    }
}

// Perform necessary reconfiguration when user activates or deactivates applets at run-time
// Call after changing settings.userApplets.active
void InkHUD::WindowManager::changeActivatedApplets()
{
    assert(menuApplet->isForeground());

    // Remove all old drawing
    // Prevents pixels getting stuck in space which forms the gap between applets
    // Unlikely in this situation, but *can* happen if notification bar shown
    clearBuffer();

    // Activate or deactivate applets
    // - to match value of settings.userApplets.active
    createUserApplets();

    // Assign the placeholder applet
    // - if applet was foreground on a tile when deactivated, swap it with a placeholder
    // - placeholder applet may be assigned to multiple tiles, if needed
    assignUserAppletsToTiles();

    // Ensure focused tile has a valid applet
    // - if focused tile's old applet was deactivated, give it a real applet, instead of placeholder
    // - reason: nextApplet() won't cycle applets if placeholder is shown
    refocusTile();

    // Restore menu
    // - its assignment was cleared (assignUserAppletsToTiles)
    if (menuApplet->isForeground())
        menuApplet->setTile(userTiles.at(settings.userTiles.focused));

    // Note: this "adhoc redraw" action which several methods use will be abstracted,
    // when WindowManager::render() gets refactored

    // Draw user applets (no refresh)
    // - recreates the "frozen" user applets, shown on tiles not showing the menu applet
    // - menu will draw over-top before next render()
    for (Tile *t : userTiles) {
        t->displayedApplet->setTile(t);          // Set dimensions; provide a reference to this tile, so it can receive pixels
        t->displayedApplet->resetDrawingSpace(); // Reset the drawing environment
        t->displayedApplet->render();            // Run the drawing operation, feeding pixels via Tile, into WindowManager
    }

    // If the battery icon is shown, ad-hoc redraw this too
    if (batteryIconApplet->isForeground()) {
        batteryIconApplet->setTile(batteryIconApplet->getTile());
        batteryIconApplet->resetDrawingSpace();
        batteryIconApplet->render();
    }
}

// Change whether the battery icon is displayed (top left corner)
// Don't toggle the OptionalFeatures value before calling this, our method handles it internally
void InkHUD::WindowManager::toggleBatteryIcon()
{
    assert(batteryIconApplet->isActive());
    settings.optionalFeatures.batteryIcon = !settings.optionalFeatures.batteryIcon; // Preserve the change between boots

    // Show or hide the applet
    if (settings.optionalFeatures.batteryIcon)
        batteryIconApplet->bringToForeground();
    else
        batteryIconApplet->sendToBackground();

    // Note: this "adhoc redraw" action which several methods use will be abstracted,
    // when WindowManager::render() gets refactored

    // Draw user applets (no refresh)
    // - recreates the "frozen" user applets, shown on tiles not showing the menu applet
    // - menu will draw over-top before next render()
    for (Tile *t : userTiles) {
        t->displayedApplet->setTile(t);          // Set dimensions; provide a reference to this tile, so it can receive pixels
        t->displayedApplet->resetDrawingSpace(); // Reset the drawing environment
        t->displayedApplet->render();            // Run the drawing operation, feeding pixels via Tile, into WindowManager
    }

    // If the battery icon is shown, ad-hoc redraw this too
    if (batteryIconApplet->isForeground()) {
        batteryIconApplet->setTile(batteryIconApplet->getTile());
        batteryIconApplet->resetDrawingSpace();
        batteryIconApplet->render();
    }
}

// Allow applets to suppress notifications
// Applets will be asked whether they approve, before a notification is shown via the NotificationApplet
// An applet might want to suppress a notification if the applet itself already displays this info
// Example: SingleMessageApplet should not approve notifications for messages, if it is in foreground
bool InkHUD::WindowManager::approveNotification(InkHUD::Notification &n)
{
    // Ask all currently displayed applets
    for (Tile *t : userTiles) {
        Applet *a = t->displayedApplet;
        if (!a->approveNotification(n))
            return false;
    }

    // Nobody objected
    return true;
}

// Set a flag, which will be picked up by runOnce, ASAP.
// This prevents an applet getting notified by an Observable, and updating immediately.
// Quite likely, other applets are also about to receive the same notification.
// Each notified applet can independently call requestUpdate(), and all share the one opportunity to render, at next runOnce
void InkHUD::WindowManager::requestUpdate(Drivers::EInk::UpdateTypes type = Drivers::EInk::UpdateTypes::UNSPECIFIED,
                                          bool async = true, bool allTiles = false)
{
    this->updateRequested = true;

    // Testing only
    // Todo: OR requested update types together, and decode it later
    if (type != Drivers::EInk::UpdateTypes::UNSPECIFIED)
        this->requestedUpdateType = type;
    if (async == false)
        this->requestedAsync = false;
    if (allTiles)
        this->requestedRenderAll = true;

    // No need to wait;
    // ideally we will run the thread as soon as we loop(),
    // after all Applets have had a chance to observe whatever event set this off
    OSThread::setIntervalFromNow(0);
    OSThread::enabled = true;
    runASAP = true;
}

// Receives rendered image data from an Applet, via a tile
// When applets render, they output pixel data relative to their own left / top edges
// They pass this pixel data to tile, which offsets the pixels, making them relative to the display left / top edges
// That data is then passed to this method, which applies any rotation, then places the pixels into the image buffer
// That image buffer is the fully-formatted data handed off to the driver
void InkHUD::WindowManager::handleTilePixel(int16_t x, int16_t y, Color c)
{
    rotatePixelCoords(&x, &y);
    setBufferPixel(x, y, c);
}

// Width of the display, relative to rotation
uint16_t InkHUD::WindowManager::getWidth()
{
    if (settings.rotation % 2)
        return driver->height;
    else
        return driver->width;
}

// Height of the display, relative to rotation
uint16_t InkHUD::WindowManager::getHeight()
{
    if (settings.rotation % 2)
        return driver->width;
    else
        return driver->height;
}

// How many user applets have been built? Includes applets which have been inactivated by user config
uint8_t InkHUD::WindowManager::getAppletCount()
{
    return userApplets.size();
}

// A tidy title for applets: used on-display in some situations
// Index is the order in the WindowManager::userApplets vector
// This is the same order that applets were added in setupNicheGraphics
const char *InkHUD::WindowManager::getAppletName(uint8_t index)
{
    return userApplets.at(index)->name;
}

// Runs at regular intervals
// WindowManager's uses of this include:
// - postponing render: until next loop(), allowing all applets to be notified of some Mesh event before render
// - queuing another render: while one is already is progress
int32_t InkHUD::WindowManager::runOnce()
{
    // If an applet asked to render, and hardware is able, lets try now
    if (updateRequested && !driver->busy()) {
        render();
    }

    // If our render() call failed, try again shortly
    // otherwise, stop our thread until next update due
    if (updateRequested)
        return 1000UL;
    else
        return OSThread::disable();
}

// Make an attempt to gather image data from some / all applets, and update the display
// Might not be possible right now, if update already is progress.
// The "force" parameter determines whether all applets will re-render, or only those which called Applet::requestUpdate()
void InkHUD::WindowManager::render(bool force)
{
    // Todo: Streamline all this. Abstractify

    if (force)
        requestedRenderAll = true;

    // Previous update still running
    // Will try again shortly, via runOnce()
    if (driver->busy())
        return;

    // If we're re-drawing all applets, clear the entire buffer,
    // otherwise system applets might leave pixels in the gutters between user applets
    if (requestedRenderAll)
        clearBuffer();

    // Autoshow
    // ---------
    // If a backgrounded applet requests update, switch it to foreground, if permitted
    // User selects which applets have this permission via on-screen menu
    // Priority is determined by the order which applets were added to WindowManager in setupNicheGraphics
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        Applet *a = userApplets.at(i);
        if (a->wantsToRender() && !a->isForeground() && settings.userApplets.autoshow[i]) {
            Tile *t = userTiles.at(settings.userTiles.focused); // Get focused tile
            t->displayedApplet->sendToBackground();             // Background whatever applet is already on the tile
            t->displayedApplet = a;                             // Assign our new applet to tile
            a->bringToForeground();                             // Foreground our new applet
            break;                                              // Only do this for one applet. Avoid conflicts.
        }
    }

    bool imageChanged = false; // If nobody requested an update, we might skip it?

    // User applets
    // -------------
    // Processed only if neither menu nor fullscreen applets shown
    if (!fullscreenTile->displayedApplet && !menuApplet->isForeground()) {

        // For each tile, offer (or force) to render the currently shown applet
        // --------------------------------------------------------------------
        // A tile's foreground applet will render if it called requestUpdate() recently
        // All foreground applets will render if force==true
        // No tile's applets will render if a fullscreen applet, or the menu applet, is displayed
        for (Tile *t : userTiles) {

            // Debugging only: runtime
            uint32_t start = millis();

            // Check whether the user applet assigned to a tile wants to render, and that it is not obscured by a system applet
            bool shouldRender = false;
            shouldRender |= requestedRenderAll;
            shouldRender |= (t->displayedApplet == placeholderApplet);
            shouldRender |= (t->displayedApplet->isForeground() && t->displayedApplet->wantsToRender());

            if (shouldRender) {
                t->displayedApplet->setTile(t);          // Sets applet dimension. Also sets which tile receives applet pixels
                t->displayedApplet->resetDrawingSpace(); // Reset the drawing environment
                t->displayedApplet->render();            // Run the drawing operation, feeding pixels via Tile, into WindowManager
                imageChanged = true;
            }

            // Debugging only: runtime
            uint32_t stop = millis();
            LOG_DEBUG("%s took %dms to render", t->displayedApplet->name, stop - start);
        }
    }

    // Testing only
    // Render battery
    // Todo: handle with other system applets
    // --------------------------------------
    if (batteryIconApplet->isForeground()) {
        if (batteryIconApplet->wantsToRender()) // Check before resetDrawingSpace() - resets flag
            imageChanged = true;
        batteryIconApplet->resetDrawingSpace();
        batteryIconApplet->render();
    }

    // Testing only
    // Render notificationApplet, always if foreground
    // Todo: handle with other system applets
    // --------------------------------------
    if (notificationApplet->isForeground()) {
        imageChanged = true;
        notificationApplet->setTile(notificationTile);
        notificationApplet->resetDrawingSpace();
        notificationApplet->render();
    }

    // Potentially render the menu applet
    // -----------------------------------
    if (menuApplet->isForeground() && menuApplet->wantsToRender()) {
        assert(menuApplet->getTile()); // Confirm that menu is assigned to a tile
        menuApplet->resetDrawingSpace();
        menuApplet->render();
        imageChanged = true; // Todo: handle refresh specially for the menu
    }

    // Check if any system applets want to render
    // ------------------------------------------------------
    for (Applet *sa : systemApplets) {
        if (sa->isForeground() && sa->wantsToRender()) {
            imageChanged = true;
            sa->resetDrawingSpace();
            sa->render();
        }
    }

    // Testing only
    // Call a simple full refresh, unless explicitly requested otherwise
    // In future: fewer full refreshes
    if (imageChanged) {
        LOG_DEBUG("Updating display");
        driver->update(imageBuffer, requestedUpdateType, requestedAsync);
    }

    // All done; display driver will do the rest
    // Tidy up - clear the request
    updateRequested = false;
    requestedRenderAll = false;
    requestedUpdateType = EInk::UpdateTypes::UNSPECIFIED;
    requestedAsync = true;
}

// Set a ready-to-draw pixel into the image buffer
// All rotations / translations have already taken place: this buffer data is formatted ready for the driver
void InkHUD::WindowManager::setBufferPixel(int16_t x, int16_t y, Color c)
{
    uint32_t byteNum = (y * imageBufferWidth) + (x / 8); // X data is 8 pixels per byte
    uint8_t bitNum = 7 - (x % 8); // Invert order: leftmost bit (most significant) is leftmost pixel of byte.

    bitWrite(imageBuffer[byteNum], bitNum, c);
}

// Applies the system-wide rotation to pixel positions
// This step is applied to image data which has already been translated by a Tile object
// This is the final step before the pixel is placed into the image buffer
// No return: values of the *x and *y parameters are modified by the method
void InkHUD::WindowManager::rotatePixelCoords(int16_t *x, int16_t *y)
{
    // Apply a global rotation to pixel locations
    int16_t x1 = 0;
    int16_t y1 = 0;
    switch (settings.rotation) {
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

// Manually fill the image buffer with WHITE
// Clears any old drawing
void InkHUD::WindowManager::clearBuffer()
{
    memset(imageBuffer, 0xFF, imageBufferHeight * imageBufferWidth);
}

// Seach for any applets which believe they are foreground, but no longer have a valid tile
// Tidies up after layout changes at runtime
void InkHUD::WindowManager::findOrphanApplets()
{
    for (uint8_t ia = 0; ia < userApplets.size(); ia++) {
        Applet *a = userApplets.at(ia);

        // Applet doesn't believe it is displayed: not orphaned
        if (!a->isForeground())
            continue;

        // Check each tile, to see if anyone claims this applet
        bool foundOwner = false;
        for (uint8_t it = 0; it < userTiles.size(); it++) {
            Tile *t = userTiles.at(it);
            // A tile claims this applet: not orphaned
            if (t->displayedApplet == a) {
                foundOwner = true;
                break;
            }
        }

        // Orphan found
        // Tell the applet that no tile is currently displaying it
        // This allows the focussed tile to cycle to this applet again by pressing user button
        if (!foundOwner)
            a->sendToBackground();
    }
}

#endif