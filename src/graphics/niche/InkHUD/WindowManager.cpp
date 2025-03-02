#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./WindowManager.h"

#include "RTC.h"
#include "mesh/NodeDB.h"

// System applets
// Must be defined in .cpp to prevent a circular dependency with Applet base class
#include "./Applets/System/BatteryIcon/BatteryIconApplet.h"
#include "./Applets/System/Logo/LogoApplet.h"
#include "./Applets/System/Menu/MenuApplet.h"
#include "./Applets/System/Notification/NotificationApplet.h"
#include "./Applets/System/Pairing/PairingApplet.h"
#include "./Applets/System/Placeholder/PlaceholderApplet.h"
#include "./Applets/System/Tips/TipsApplet.h"

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

    logoApplet->showBootScreen();
    forceUpdate(Drivers::EInk::FULL, false); // Update now, and wait here until complete

    deepSleepObserver.observe(&notifyDeepSleep);
    rebootObserver.observe(&notifyReboot);
    textMessageObserver.observe(textMessageModule);
#ifdef ARCH_ESP32
    lightSleepObserver.observe(&notifyLightSleep);
#endif
}

// Set-up special "system applets"
// These handle things like bootscreen, pop-up notifications etc
// They are processed separately from the user applets, because they might need to do "weird things"
// They also won't be activated or deactivated
void InkHUD::WindowManager::createSystemApplets()
{
    logoApplet = new LogoApplet;
    pairingApplet = new PairingApplet;
    tipsApplet = new TipsApplet;
    notificationApplet = new NotificationApplet;
    batteryIconApplet = new BatteryIconApplet;
    menuApplet = new MenuApplet;
    placeholderApplet = new PlaceholderApplet;

    // System applets are always active
    logoApplet->activate();
    pairingApplet->activate();
    tipsApplet->activate();
    notificationApplet->activate();
    batteryIconApplet->activate();
    menuApplet->activate();
    placeholderApplet->activate();

    // Add to the systemApplets vector
    // Although system applets often need special handling, sometimes we can process them en-masse with this vector
    // e.g. rendering, raising events
    // Order of these entries determines Z-Index when rendering
    systemApplets.push_back(logoApplet);
    systemApplets.push_back(pairingApplet);
    systemApplets.push_back(tipsApplet);
    systemApplets.push_back(batteryIconApplet);
    systemApplets.push_back(menuApplet);
    systemApplets.push_back(notificationApplet);
    // Note: placeholder applet is technically a system applet, but it renders in WindowManager::renderPlaceholders
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

// Assign a system applet to the fullscreen tile
// Rendering of user tiles is suspended when the fullscreen tile is occupied
void InkHUD::WindowManager::claimFullscreen(InkHUD::Applet *a)
{
    // Make sure that only system applets use the fullscreen tile
    bool isSystemApplet = false;
    for (Applet *sa : systemApplets) {
        if (sa == a) {
            isSystemApplet = true;
            break;
        }
    }
    assert(isSystemApplet);

    fullscreenTile->assignApplet(a);
}

// Clear the fullscreen tile, unlinking whichever system applet is assigned
// This allows the normal rendering of user tiles to resume
void InkHUD::WindowManager::releaseFullscreen()
{
    // Make sure the applet is ready to release the tile
    assert(!fullscreenTile->getAssignedApplet()->isForeground());

    // Break the link between the applet and the fullscreen tile
    fullscreenTile->assignApplet(nullptr);
}

// Some system applets can be assigned to a tile at boot
// These are applets which do have their own tile, and whose assignment never changes
// Applets which:
// - share the fullscreen tile (e.g. logoApplet, pairingApplet),
// - render on user tiles (e.g. menuApplet, placeholderApplet),
// are assigned to the tile only when needed
void InkHUD::WindowManager::assignSystemAppletsToTiles()
{
    notificationTile->assignApplet(notificationApplet);
    batteryIconTile->assignApplet(batteryIconApplet);
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
        // otherwise assign nullptr, which will render specially using placeholderApplet
        if (canRestore) {
            Applet *a = userApplets.at(oldIndex);
            t->assignApplet(a);
            a->bringToForeground();
        } else {
            t->assignApplet(nullptr);
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

    // Give "focused tile" a valid applet
    // - scan for another valid applet, which we can addSubstitution
    // - reason: nextApplet() won't cycle if no applet is assigned
    Tile *focusedTile = userTiles.at(settings.userTiles.focused);
    if (!focusedTile->getAssignedApplet()) {
        // Search for available applets
        for (uint8_t i = 0; i < userApplets.size(); i++) {
            Applet *a = userApplets.at(i);
            if (a->isActive() && !a->isForeground()) {
                // Found a suitable applet
                // Assign it to the focused tile
                focusedTile->assignApplet(a);
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
    // Notify all applets that we're shutting down
    for (Applet *ua : userApplets) {
        ua->onDeactivate();
        ua->onShutdown();
    }
    for (Applet *sa : userApplets) {
        // Note: no onDeactivate. System applets are always active.
        sa->onShutdown();
    }

    // User has successfull executed a safe shutdown
    // We don't need to nag at boot anymore
    settings.tips.safeShutdownSeen = true;

    saveDataToFlash();

    // Display the shutdown screen, and wait here until the update is complete
    logoApplet->showShutdownScreen();
    forceUpdate(Drivers::EInk::UpdateTypes::FULL, false);

    return 0; // We agree: deep sleep now
}

// Callback for rebootObserver
// Same as shutdown, without drawing the logoApplet
// Makes sure we don't lose message history / InkHUD config
int InkHUD::WindowManager::beforeReboot(void *unused)
{

    // Notify all applets that we're "shutting down"
    // They don't need to know that it's really a reboot
    for (Applet *a : userApplets) {
        a->onDeactivate();
        a->onShutdown();
    }
    for (Applet *sa : userApplets) {
        // Note: no onDeactivate. System applets are always active.
        sa->onShutdown();
    }

    saveDataToFlash();

    return 0; // No special status to report. Ignored anyway by this Observable
}

#ifdef ARCH_ESP32
// Callback for lightSleepObserver
// Make sure the display is not partway through an update when we begin light sleep
// This is because some displays require active input from us to terminate the update process, and protect the panel hardware
int InkHUD::WindowManager::beforeLightSleep(void *unused)
{
    if (driver->busy()) {
        LOG_INFO("Waiting for display");
        driver->await(); // Wait here for update to complete
    }

    return 0; // No special status to report. Ignored anyway by this Observable
}
#endif

// Callback when a new text message is received
// Caches the most recently received message, for use by applets
// Rx does not trigger a save to flash, however the data *will* be saved alongside other during shutdown, etc.
// Note: this is different from devicestate.rx_text_message, which may contain an *outgoing* message
int InkHUD::WindowManager::onReceiveTextMessage(const meshtastic_MeshPacket *packet)
{
    // Short circuit: don't store outgoing messages
    if (getFrom(packet) == nodeDB->getNodeNum())
        return 0;

    // Short circuit: don't store "emoji reactions"
    // Possibly some implemetation of this in future?
    if (packet->decoded.emoji)
        return 0;

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
        forceUpdate(EInk::UpdateTypes::FULL); // Redraw everything, to clear the notification
    }

    // If window manager is locked: lock owner handles button
    else if (lockOwner)
        lockOwner->onButtonShortPress();

    // Normally: next applet
    else
        nextApplet();
}

// Triggered by an input source when a long-press fires
// The input source is a separate component; not part of InkHUD
// It is connected in setupNicheGraphics()
// Note: input source should raise this while button still held
void InkHUD::WindowManager::handleButtonLong()
{
    if (lockOwner)
        lockOwner->onButtonLongPress();

    else
        menuApplet->show(userTiles.at(settings.userTiles.focused));
}

// On the currently focussed tile: cycle to the next available user applet
// Applets available for this must be activated, and not already displayed on another tile
void InkHUD::WindowManager::nextApplet()
{
    Tile *t = userTiles.at(settings.userTiles.focused);

    // Abort if zero applets available
    // nullptr means WindowManager::refocusTile determined that there were no available applets
    if (!t->getAssignedApplet())
        return;

    // Find the index of the applet currently shown on the tile
    uint8_t appletIndex = -1;
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        if (userApplets.at(i) == t->getAssignedApplet()) {
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
    t->getAssignedApplet()->sendToBackground();
    t->assignApplet(nextValidApplet);
    nextValidApplet->bringToForeground();
    forceUpdate(EInk::UpdateTypes::FAST); // bringToForeground already requested, but we're manually forcing FAST
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

    // Seems like some system applet other than menu is open. Pairing? Booting?
    if (!canRequestUpdate())
        return;

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
    if (menuApplet->isForeground()) {
        Tile *ft = userTiles.at(settings.userTiles.focused);
        menuApplet->show(ft);
    }

    // Force-render
    // - redraw all applets
    forceUpdate(EInk::UpdateTypes::FAST);
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
    if (menuApplet->isForeground()) {
        Tile *ft = userTiles.at(settings.userTiles.focused);
        menuApplet->show(ft);
    }

    // Force-render
    // - redraw all applets
    forceUpdate(EInk::UpdateTypes::FAST);
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
    forceUpdate(EInk::UpdateTypes::FAST);
}

// Allow applets to suppress notifications
// Applets will be asked whether they approve, before a notification is shown via the NotificationApplet
// An applet might want to suppress a notification if the applet itself already displays this info
// Example: AllMessageApplet should not approve notifications for messages, if it is in foreground
bool InkHUD::WindowManager::approveNotification(InkHUD::Notification &n)
{
    // Ask all currently displayed applets
    for (Tile *ut : userTiles) {
        Applet *ua = ut->getAssignedApplet();
        if (ua && !ua->approveNotification(n))
            return false;
    }

    // Nobody objected
    return true;
}

// Set a flag, which will be picked up by runOnce, ASAP.
// Quite likely, multiple applets will all want to respond to one event (Observable, etc)
// Each affected applet can independently call requestUpdate(), and all share the one opportunity to render, at next runOnce
void InkHUD::WindowManager::requestUpdate()
{
    requestingUpdate = true;

    // We will run the thread as soon as we loop(),
    // after all Applets have had a chance to observe whatever event set this off
    OSThread::setIntervalFromNow(0);
    OSThread::enabled = true;
    runASAP = true;
}

// requestUpdate will not actually update if no requests were made by applets which are actually visible
// This can occur, because applets requestUpdate even from the background,
// in case the user's autoshow settings permit them to be moved to foreground.
// Sometimes, however, we will want to trigger a display update manually, in the absense of any sort of applet event
// Display health, for example.
// In these situations, we use forceUpdate
void InkHUD::WindowManager::forceUpdate(EInk::UpdateTypes type, bool async)
{
    requestingUpdate = true;
    forcingUpdate = true;
    forcedUpdateType = type;

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

// Allows a system applet to prevent other applets from temporarily requesting updates
// All user applets will honor this. Some system applets might not, although they probably should
// WindowManager::forceUpdate will ignore this lock
void InkHUD::WindowManager::lock(Applet *owner)
{
    // Only one system applet may lock render at once
    assert(!lockOwner);

    // Only system applets may lock rendering
    for (Applet *a : userApplets)
        assert(owner != a);

    lockOwner = owner;
}

// Remove a lock placed by a system applet, which prevents other applets from rendering
void InkHUD::WindowManager::unlock(Applet *owner)
{
    assert(lockOwner = owner);
    lockOwner = nullptr;

    // Raise this as an event (system applets only)
    // - in case applet waiting for lock
    // - in case applet relinquished its lock earlier, and wants it back
    for (Applet *sa : systemApplets) {
        // Don't raise event for the applet which is calling unlock
        // - avoid loop of unlock->lock (some implementations of Applet::onLockAvailable)
        if (sa != owner)
            sa->onLockAvailable();
    }
}

// Is an applet blocked from requesting update by a current lock?
// Applets are allowed to request updates if there is no lock, or if they are the owner of the lock
// If a == nullptr, checks permission "for everyone and anyone"
bool InkHUD::WindowManager::canRequestUpdate(Applet *a)
{
    if (!lockOwner)
        return true;
    else if (lockOwner == a)
        return true;
    else
        return false;
}

// Get the applet which is currently locking rendering
// We might be able to convince it release its lock, if we want it instead
InkHUD::Applet *InkHUD::WindowManager::whoLocked()
{
    return WindowManager::lockOwner;
}

// Runs at regular intervals
// WindowManager's uses of this include:
// - postponing render: until next loop(), allowing all applets to be notified of some Mesh event before render
// - queuing another render: while one is already is progress
int32_t InkHUD::WindowManager::runOnce()
{
    // If an applet asked to render, and hardware is able, lets try now
    if (requestingUpdate && !driver->busy()) {
        render();
    }

    // If our render() call failed, try again shortly
    // otherwise, stop our thread until next update due
    if (requestingUpdate)
        return 250UL;
    else
        return OSThread::disable();
}

// Some applets may be permitted to bring themselved to foreground, to show new data
// User selects which applets have this permission via on-screen menu
// Priority is determined by the order which applets were added to WindowManager in setupNicheGraphics
// We will only autoshow one applet
void InkHUD::WindowManager::autoshow()
{
    for (uint8_t i = 0; i < userApplets.size(); i++) {
        Applet *a = userApplets.at(i);
        if (a->wantsToAutoshow()                // Applet wants to become foreground
            && !a->isForeground()               // Not yet foreground
            && settings.userApplets.autoshow[i] // User permits this applet to autoshow
            && canRequestUpdate())              // Updates not currently blocked by system applet
        {
            Tile *t = userTiles.at(settings.userTiles.focused); // Get focused tile
            t->getAssignedApplet()->sendToBackground();         // Background whichever applet is already on the tile
            t->assignApplet(a);                                 // Assign our new applet to tile
            a->bringToForeground();                             // Foreground our new applet

            // Check if autoshown applet shows the same information as notification intended to
            // In this case, we can dismiss the notification before it is shown
            // Note: we are re-running the approval process. This normally occurs when the notification is initially triggered.
            if (notificationApplet->isForeground() && !notificationApplet->isApproved())
                notificationApplet->dismiss();

            break; // One autoshow only! Avoid conflicts
        }
    }
}

// Check whether an update is justified
// We usually require that a foreground applet requested the update,
// but forceUpdate call will bypass these checks.
// Abstraction for WindowManager::render only
bool InkHUD::WindowManager::shouldUpdate()
{
    bool should = false;

    // via forceUpdate
    should |= forcingUpdate;

    // via user applet
    for (Tile *ut : userTiles) {
        Applet *ua = ut->getAssignedApplet();
        if (ua                     // Tile has valid applet
            && ua->wantsToRender() // This applet requested display update
            && ua->isForeground()  // This applet is currently shown
            && canRequestUpdate()) // Requests are not currently locked
        {
            should = true;
            break;
        }
    }

    // via system applet
    for (Applet *sa : systemApplets) {
        if (sa->wantsToRender()      // This applet requested
            && sa->isForeground()    // This applet is currently shown
            && canRequestUpdate(sa)) // Requests are not currently locked, or this applet owns the lock
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
// Abstraction for WindowManager::render only
Drivers::EInk::UpdateTypes InkHUD::WindowManager::selectUpdateType()
{
    // Ask applets which update type they would prefer
    // Some update types take priority over others
    EInk::UpdateTypes type = EInk::UpdateTypes::UNSPECIFIED;
    if (forcingUpdate) {
        // Update type was manually specified via forceUpdate
        type = forcedUpdateType;
    } else {
        // User applets
        for (Tile *ut : userTiles) {
            Applet *ua = ut->getAssignedApplet();
            if (ua && ua->isForeground() && canRequestUpdate())
                type = mediator.prioritize(type, ua->wantsUpdateType());
        }
        // System Applets
        for (Applet *sa : systemApplets) {
            if (sa->isForeground() && canRequestUpdate(sa))
                type = mediator.prioritize(type, sa->wantsUpdateType());
        }
    }

    // Tell the mediator what update type the applets deciced on,
    // find out what update type the mediator will actually allow us to have
    type = mediator.evaluate(type);

    return type;
}

// Run the drawing operations of any user applets which are currently displayed
// Pixel output is placed into the framebuffer, ready for handoff to the EInk driver
// Abstraction for WindowManager::render only
void InkHUD::WindowManager::renderUserApplets()
{
    // Don't render any user applets if the screen is covered by a system applet using the fullscreen tile
    if (fullscreenTile->getAssignedApplet())
        return;

    // For each tile
    for (Tile *ut : userTiles) {
        Applet *ua = ut->getAssignedApplet(); // Get the applet on the tile

        // Don't render if tile has no applet. Handled in renderPlaceholders
        if (!ua)
            continue;

        // Don't render the menu applet, Handled by renderSystemApplets
        if (ua == menuApplet)
            continue;

        uint32_t start = millis();
        ua->render(); // Draw!
        uint32_t stop = millis();
        LOG_DEBUG("%s took %dms to render", ua->name, stop - start);
    }
}

// Run the drawing operations of any system applets which are currently displayed
// Pixel output is placed into the framebuffer, ready for handoff to the EInk driver
// Abstraction for WindowManager::render only
void InkHUD::WindowManager::renderSystemApplets()
{
    // Each system applet
    for (Applet *sa : systemApplets) {
        // Skip if not shown
        if (!sa->isForeground())
            continue;

        // Don't draw the battery overtop the menu
        // Todo: smarter way to handle this
        if (sa == batteryIconApplet && menuApplet->isForeground())
            continue;

        // Skip applet if fullscreen tile is in use, but not used by this applet
        // Applet is "obscured"
        if (fullscreenTile->getAssignedApplet() && fullscreenTile->getAssignedApplet() != sa)
            continue;

        // uint32_t start = millis(); // Debugging only: runtime
        sa->render(); // Draw!
        // uint32_t stop = millis();  // Debugging only: runtime
        // LOG_DEBUG("%s (system) took %dms to render", (sa->name == nullptr) ? "Unnamed" : sa->name, stop - start);
    }
}

// In some situations (e.g. layout or applet selection changes),
// a user tile can end up without an assigned applet.
// In this case, we will fill the empty space with diagonal lines.
void InkHUD::WindowManager::renderPlaceholders()
{
    // Don't draw if obscured by the fullscreen tile
    if (fullscreenTile->getAssignedApplet())
        return;

    for (Tile *ut : userTiles) {
        // If no applet assigned
        if (!ut->getAssignedApplet()) {
            ut->assignApplet(placeholderApplet);
            placeholderApplet->render();
            ut->assignApplet(nullptr);
        }
    }
}

// Make an attempt to gather image data from some / all applets, and update the display
// Might not be possible right now, if update already is progress.
void InkHUD::WindowManager::render(bool async)
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

    // (Potentially) change applet to display new info,
    // then check if this newly displayed applet makes a pending notification redundant
    autoshow();

    // If an update is justified.
    // We don't know this until after autoshow has run, as new applets may now be in foreground
    if (shouldUpdate()) {

        // Decide which technique the display will use to change image
        EInk::UpdateTypes updateType = selectUpdateType();

        // Render the new image
        clearBuffer();
        renderUserApplets();
        renderSystemApplets();
        renderPlaceholders();

        // Tell display to begin process of drawing new image
        LOG_INFO("Updating display");
        driver->update(imageBuffer, updateType);

        // If not async, wait here until the update is complete
        if (!async)
            driver->await();
    } else
        LOG_DEBUG("Not updating display");

    // Our part is done now.
    // If update is async, the display hardware is still performing the update process,
    // but that's all handled by NicheGraphics::Drivers::EInk

    // Tidy up, ready for a new request
    requestingUpdate = false;
    forcingUpdate = false;
    forcedUpdateType = EInk::UpdateTypes::UNSPECIFIED;
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
            if (t->getAssignedApplet() == a) {
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