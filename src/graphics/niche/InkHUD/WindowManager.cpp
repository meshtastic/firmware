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

// Sets the ideal ratio of FAST updates to FULL updates
// We want as many FAST updates as possible, without causing gradual degradation of the display
// If explicitly requested, number of FAST updates may exceed fastPerFull value.
// In this case, the stressMultiplier is applied, causing the "FULL update debt" to increase by more than normal
// The stressMultplier helps the display recover from particularly taxing periods of use
// (Default arguments of 5,2 are very conservative values)
void InkHUD::WindowManager::setDisplayResilience(uint8_t fastPerFull = 5, float stressMultiplier = 2.0)
{
    mediator.fastPerFull = fastPerFull;
    mediator.stressMultiplier = stressMultiplier;
}

// Register a user applet with the WindowManager
// This is called in setupNicheGraphics()
// This should be the only time that specific user applets are mentioned in the code
// If a user applet is not added with this method, its code should not be built
void InkHUD::WindowManager::addApplet(const char *name, Applet *a, bool defaultActive, bool defaultAutoshow, uint8_t onTile)
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

    // If specified, mark this as the default applet for a given tile index
    // Used only to avoid placeholder applet "out of the box", when default settings have more than one tile
    if (onTile != (uint8_t)-1)
        settings.userTiles.displayedUserApplet[onTile] = userApplets.size() - 1;

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

    loadDataFromFlash();

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
    notificationApplet->activate();
    batteryIconApplet->activate();
    menuApplet->activate();
    placeholderApplet->activate();
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
    // Set "assignedApplet" property
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
            t->assignedApplet = userApplets.at(oldIndex);
            t->assignedApplet->bringToForeground();
        } else {
            t->assignedApplet = placeholderApplet;
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
    if (focusedTile->assignedApplet == placeholderApplet) {
        // Search for available applets
        for (uint8_t i = 0; i < userApplets.size(); i++) {
            Applet *a = userApplets.at(i);
            if (a->isActive() && !a->isForeground()) {
                // Found a suitable applet
                // Assign it to the focused tile
                focusedTile->assignedApplet = a;
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

    saveDataToFlash();
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

        // Determine whether the message is broadcast or a DM
        // Store this info to prevent confusion after a reboot
        // Avoids need to compare timestamps, because of situation where "future" messages block newly received, if time not set
        latestMessage.wasBroadcast = isBroadcast(packet->to);

        // Pick the appropriate variable to store the message in
        MessageStore::Message *storedMessage = latestMessage.wasBroadcast ? &latestMessage.broadcast : &latestMessage.dm;

        // Store nodenum of the sender
        // Applets can use this to fetch user data from nodedb, if they want
        storedMessage->sender = packet->from;

        // Store the time (epoch seconds) when message received
        storedMessage->timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time

        // Store the channel
        // - (potentially) used to determine whether notification shows
        // - (potentially) used to determine which applet to focus
        storedMessage->channelIndex = packet->channel;

        // Store the text
        // Need to specify manually how many bytes, because source not null-terminated
        storedMessage->text =
            std::string(&packet->decoded.payload.bytes[0], &packet->decoded.payload.bytes[packet->decoded.payload.size]);
    }

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

// Triggered by an input source when a short-press fires
// The input source is a separate component; not part of InkHUD
// It is connected in setupNicheGraphics()
void InkHUD::WindowManager::handleButtonShort()
{
    // If notification is open: close it
    if (notificationApplet->isForeground()) {
        notificationApplet->dismiss();
        requestUpdate(EInk::UpdateTypes::FULL, true); // Redraw everything, to clear the notification
    }

    // Normally: next applet
    else if (!menuApplet->isForeground())
        nextApplet();

    // If menu is open: scroll menu
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
        Tile *t = userTiles.at(settings.userTiles.focused);
        Applet *ua = t->assignedApplet; // User applet whose tile we're borrowing

        // Hide the current applet
        ua->sendToBackground();

        // Tell menu applet to render using our borrowed applet's tile
        menuApplet->setTile(t);
        menuApplet->bringToForeground();

        // bringToForeground has already requested update, but we're updating it to FAST, for guaranteed responsiveness
        requestUpdate(Drivers::EInk::UpdateTypes::FAST, false);
    }

    // Or let the menu handle it
    else
        menuApplet->onButtonLongPress();
}

// On the currently focussed tile: cycle to the next available user applet
// Applets available for this must be activated, and not already displayed on another tile
void InkHUD::WindowManager::nextApplet()
{
    Tile *t = userTiles.at(settings.userTiles.focused);

    // Short circuit: zero applets available
    if (t->assignedApplet == placeholderApplet)
        return;

    // Find the index of the applet currently shown on the tile
    uint8_t appletIndex = -1;
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        if (userApplets.at(i) == t->assignedApplet) {
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
    t->assignedApplet->sendToBackground();
    t->assignedApplet = nextValidApplet;
    t->assignedApplet->bringToForeground();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, false); // bringToForeground already requested. Just upgrading to FAST
}

// Focus on a different tile
// The "focused tile" is the one which cycles applets on user button press,
// and the one where the menu will be displayed
// Note: this method is only used by an aux button
// The menuApplet manually performs a subset of these actions, to avoid disturbing the stale image on adjacent tiles
void InkHUD::WindowManager::nextTile()
{
    // Close the menu applet if open
    // We done *really* want to do this, but it simplifies handling *a lot*
    if (menuApplet->isForeground())
        menuApplet->sendToBackground();

    // Swap to next tile
    settings.userTiles.focused = (settings.userTiles.focused + 1) % settings.userTiles.count;

    // Make sure that we don't get stuck on the placeholder tile
    // changeLayout reassigns applets to tiles
    changeLayout();

    // Ask the tile to draw an indicator showing which tile is now focused
    // Requests a render
    userTiles.at(settings.userTiles.focused)->requestHighlight();
}

// Perform necessary reconfiguration when user changes number of tiles (or rotation) at run-time
// Call after changing settings.tiles.count
void InkHUD::WindowManager::changeLayout()
{
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

    // Force-render
    // - redraw all applets
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, true);
}

// Perform necessary reconfiguration when user activates or deactivates applets at run-time
// Call after changing settings.userApplets.active
void InkHUD::WindowManager::changeActivatedApplets()
{
    assert(menuApplet->isForeground());

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

    // Force-render
    // - redraw all applets
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, true);
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

    // Force-render
    // - redraw all applets
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, true);
}

// Allow applets to suppress notifications
// Applets will be asked whether they approve, before a notification is shown via the NotificationApplet
// An applet might want to suppress a notification if the applet itself already displays this info
// Example: AllMessageApplet should not approve notifications for messages, if it is in foreground
bool InkHUD::WindowManager::approveNotification(InkHUD::Notification &n)
{
    // Ask all currently displayed applets
    for (Tile *t : userTiles) {
        Applet *a = t->assignedApplet;
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
                                          bool allTiles = false)
{
    this->updateRequested = true;

    // Todo: priority of these types
    if (type != Drivers::EInk::UpdateTypes::UNSPECIFIED)
        this->requestedUpdateType = type;
    if (allTiles)
        this->requestedRenderAll = true;

    // We will run the thread as soon as we loop(),
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
        return 250UL;
    else
        return OSThread::disable();
}

// Some applets may be permitted to bring themselved to foreground, to show new data
// User selects which applets have this permission via on-screen menu
// Priority is determined by the order which applets were added to WindowManager in setupNicheGraphics
// We will only autoshow one applet, but we need to check wantsToAutoshow for all applets, as this clears the flag,
// in case of a conflict which has not been honored.
void InkHUD::WindowManager::autoshow()
{

    bool autoshown = false;
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        Applet *a = userApplets.at(i);
        bool wants = a->wantsToAutoshow(); // Call for every applet: clears the flag

        if (!autoshown && wants && !a->isForeground() && settings.userApplets.autoshow[i]) {
            Tile *t = userTiles.at(settings.userTiles.focused); // Get focused tile
            t->assignedApplet->sendToBackground();              // Background whatever applet is already on the tile
            t->assignedApplet = a;                              // Assign our new applet to tile
            a->bringToForeground();                             // Foreground our new applet
            autoshown = true;                                   // No more autoshows now until next render
            // Keep looping though, to clear the wantAutoshow flag for all applets
        }
    }

    // Check if autoshow has shown the same information as notification intended to
    // In this case, we can dismiss the notification before it is shown
    if (autoshown && notificationApplet->isForeground() && !notificationApplet->isApproved())
        notificationApplet->dismiss();
}

// Renders whichever applets are assigned to userTiles
// Applets will normally only render if they recently called requestUpdate
// Applets may be forced to render at any time, though
// Returns true if any of the user applets wanted to update, and are currently foreground (after autoshow)
bool InkHUD::WindowManager::renderUserApplets()
{
    bool updateNeeded = false;

    // For each applet on a user tile
    for (Tile *t : userTiles) {
        Applet *a = t->assignedApplet;

        // Debugging only: runtime
        uint32_t start = millis();

        // Decide whether to render
        bool shouldRender = false;
        shouldRender |= requestedRenderAll;
        shouldRender |= a->isForeground() && a->wantsToRender() && !menuApplet->isForeground();

        // If decided to render
        if (shouldRender) {
            updateNeeded = true; // Tells WindowManager that E-Ink should update
            a->setTile(t);       // Make sure applet knows what region of display to use
            a->render();         // Draw!

            // Debugging only: runtime
            uint32_t stop = millis();
            LOG_DEBUG("%s took %dms to render", t->assignedApplet->name, stop - start);
        }
    }

    // Tell WindowManager whether any of our applets actually drew anything new
    return updateNeeded;
}

// Render most of the unique applets which have special behavior
// These applets are expected to know internally which tile they want
// Some of these features are optional. This manifests as foreground / background.
// Returns true if any of the applets do actually want to update for their own sake
bool InkHUD::WindowManager::renderSystemApplets()
{
    // Debugging only: runtime
    uint32_t start = millis();
    uint8_t renderCount = 0;

    bool updateNeeded = false;

    // placeholderApplet is technically a system applet,
    // but processed in renderUserApplets, not here

    // Battery Icon
    // - overlay: drawn regardless of wantsToRender
    // - might want to render though, if battery level changed
    if (batteryIconApplet->isForeground()) {
        if (batteryIconApplet->wantsToRender())
            updateNeeded = true;
        batteryIconApplet->render();
        renderCount++;
    }

    // Notification
    // - overlay: drawn regardless of wantsToRender
    // - might want to render though, if new notification
    if (notificationApplet->isForeground()) {
        if (notificationApplet->wantsToRender())
            updateNeeded = true;
        notificationApplet->render();
        renderCount++;
    }

    // Menu applet
    if (menuApplet->isForeground() && menuApplet->wantsToRender()) {
        updateNeeded = true;
        menuApplet->render();
        renderCount++;
    }

    // Boot screen
    if (bootscreenApplet->isForeground() && bootscreenApplet->wantsToRender()) {
        updateNeeded = true;
        bootscreenApplet->render();
        renderCount++;
    }

    // Debugging only: runtime
    uint32_t stop = millis();
    if (renderCount > 0)
        LOG_DEBUG("System applets (%d) took %dms total to render", renderCount, stop - start);

    // Tell WindowManager whether any of our applets want the display to update
    // Some of them (overlays) may have drawn regardless
    // This value is considered alongside user applets' return value, and the forced update flag
    return updateNeeded;
}

// Make an attempt to gather image data from some / all applets, and update the display
// Might not be possible right now, if update already is progress.
void InkHUD::WindowManager::render()
{
    // Previous update still running
    // Will try again shortly, via runOnce()
    if (driver->busy())
        return;

    // Whether an update will actually take place
    // Can't be certain about this any earlier,
    // because we don't know exactly which applet(s) are foreground until autoshow runs
    bool updateNeeded = false;

    // If we're re-drawing all applets, clear the entire buffer,
    // otherwise system applets might leave pixels in the gutters between user applets
    if (requestedRenderAll) {
        clearBuffer();
        updateNeeded = true;
    }

    // (Potentially) change applet to display new info,
    // then check if this newly displayed applet makes a pending noftification redundant
    autoshow();

    // The "normal" applets
    updateNeeded |= renderUserApplets();

    // All the unique applets with special handling, e.g. menuApplet, batteryIconApplet etc
    updateNeeded |= renderSystemApplets();

    // Update the display, if some of the image did change
    if (updateNeeded) {
        Drivers::EInk::UpdateTypes type = mediator.evaluate(requestedUpdateType);
        LOG_INFO("Updating");
        driver->update(imageBuffer, type);
    } else
        LOG_DEBUG("Not updating");

    // All done; display driver will do the rest
    // Tidy up - clear the request
    updateRequested = false;
    requestedRenderAll = false;
    requestedUpdateType = EInk::UpdateTypes::UNSPECIFIED;
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
            if (t->assignedApplet == a) {
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