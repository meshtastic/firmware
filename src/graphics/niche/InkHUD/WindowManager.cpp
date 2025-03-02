#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./WindowManager.h"

#include "./Applets/System/BatteryIcon/BatteryIconApplet.h"
#include "./Applets/System/Logo/LogoApplet.h"
#include "./Applets/System/Menu/MenuApplet.h"
#include "./Applets/System/Notification/NotificationApplet.h"
#include "./Applets/System/Pairing/PairingApplet.h"
#include "./Applets/System/Placeholder/PlaceholderApplet.h"
#include "./Applets/System/Tips/TipsApplet.h"
#include "./SystemApplet.h"

using namespace NicheGraphics;

InkHUD::WindowManager::WindowManager()
{
    // Convenient references
    inkhud = InkHUD::getInstance();
    settings = &inkhud->persistence->settings;
}

// Register a user applet with InkHUD
// This is called in setupNicheGraphics()
// This should be the only time that specific user applets are mentioned in the code
// If a user applet is not added with this method, its code should not be built
// Call before begin
void InkHUD::WindowManager::addApplet(const char *name, Applet *a, bool defaultActive, bool defaultAutoshow, uint8_t onTile)
{
    inkhud->userApplets.push_back(a);

    // If requested, mark in settings that this applet should be active by default
    // This means that it will be available for the user to cycle to with short-press of the button
    // This is the default state only: user can activate or deactivate applets through the menu.
    // User's choice of active applets is stored in settings, and will be honored instead of these defaults, if present
    if (defaultActive)
        settings->userApplets.active[inkhud->userApplets.size() - 1] = true;

    // If requested, mark in settings that this applet should "autoshow" by default
    // This means that the applet will be automatically brought to foreground when it has new data to show
    // This is the default state only: user can select which applets have this behavior through the menu
    // User's selection is stored in settings, and will be honored instead of these defaults, if present
    if (defaultAutoshow)
        settings->userApplets.autoshow[inkhud->userApplets.size() - 1] = true;

    // If specified, mark this as the default applet for a given tile index
    // Used only to avoid placeholder applet "out of the box", when default settings have more than one tile
    if (onTile != (uint8_t)-1)
        settings->userTiles.displayedUserApplet[onTile] = inkhud->userApplets.size() - 1;

    // The label that will be show in the applet selection menu, on the device
    a->name = name;
}

// Initial configuration at startup
void InkHUD::WindowManager::begin()
{
    assert(inkhud);

    createSystemApplets();
    placeSystemTiles();

    createUserApplets();
    createUserTiles();
    placeUserTiles();
    assignUserAppletsToTiles();
    refocusTile();
}

// Focus on a different tile
// The "focused tile" is the one which cycles applets on user button press,
// and the one where the menu will be displayed
void InkHUD::WindowManager::nextTile()
{
    // Close the menu applet if open
    // We don't *really* want to do this, but it simplifies handling *a lot*
    MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
    bool menuWasOpen = false;
    if (menu->isForeground()) {
        menu->sendToBackground();
        menuWasOpen = true;
    }

    // Swap to next tile
    settings->userTiles.focused = (settings->userTiles.focused + 1) % settings->userTiles.count;

    // Make sure that we don't get stuck on the placeholder tile
    refocusTile();

    if (menuWasOpen)
        menu->show(userTiles.at(settings->userTiles.focused));

    // Ask the tile to draw an indicator showing which tile is now focused
    // Requests a render
    // We only draw this indicator if the device uses an aux button to switch tiles.
    // Assume aux button is used to switch tiles if the "next tile" menu item is hidden
    if (!settings->optionalMenuItems.nextTile)
        userTiles.at(settings->userTiles.focused)->requestHighlight();
}

// Show the menu (on the the focused tile)
// The applet previously displayed there will be restored once the menu closes
void InkHUD::WindowManager::openMenu()
{
    MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
    menu->show(userTiles.at(settings->userTiles.focused));
}

// On the currently focussed tile: cycle to the next available user applet
// Applets available for this must be activated, and not already displayed on another tile
void InkHUD::WindowManager::nextApplet()
{
    Tile *t = userTiles.at(settings->userTiles.focused);

    // Abort if zero applets available
    // nullptr means WindowManager::refocusTile determined that there were no available applets
    if (!t->getAssignedApplet())
        return;

    // Find the index of the applet currently shown on the tile
    uint8_t appletIndex = -1;
    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        if (inkhud->userApplets.at(i) == t->getAssignedApplet()) {
            appletIndex = i;
            break;
        }
    }

    // Confirm that we did find the applet
    assert(appletIndex != (uint8_t)-1);

    // Iterate forward through the WindowManager::applets, looking for the next valid applet
    Applet *nextValidApplet = nullptr;
    for (uint8_t i = 1; i < inkhud->userApplets.size(); i++) {
        uint8_t newAppletIndex = (appletIndex + i) % inkhud->userApplets.size();
        Applet *a = inkhud->userApplets.at(newAppletIndex);

        // Looking for an applet which is active (enabled by user), but currently in background
        if (a->isActive() && !a->isForeground()) {
            nextValidApplet = a;
            settings->userTiles.displayedUserApplet[settings->userTiles.focused] =
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
    inkhud->forceUpdate(EInk::UpdateTypes::FAST); // bringToForeground already requested, but we're manually forcing FAST
}

// Rotate the display image by 90 degrees
void InkHUD::WindowManager::rotate()
{
    settings->rotation = (settings->rotation + 1) % 4;
    changeLayout();
}

// Change whether the battery icon is displayed (top right corner)
// Don't toggle the OptionalFeatures value before calling this, our method handles it internally
void InkHUD::WindowManager::toggleBatteryIcon()
{
    BatteryIconApplet *batteryIcon = (BatteryIconApplet *)inkhud->getSystemApplet("BatteryIcon");

    settings->optionalFeatures.batteryIcon = !settings->optionalFeatures.batteryIcon; // Preserve the change between boots

    // Show or hide the applet
    if (settings->optionalFeatures.batteryIcon)
        batteryIcon->bringToForeground();
    else
        batteryIcon->sendToBackground();

    // Force-render
    // - redraw all applets
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
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
    MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
    if (menu->isForeground()) {
        Tile *ft = userTiles.at(settings->userTiles.focused);
        menu->show(ft);
    }

    // Force-render
    // - redraw all applets
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

// Perform necessary reconfiguration when user activates or deactivates applets at run-time
// Call after changing settings.userApplets.active
void InkHUD::WindowManager::changeActivatedApplets()
{
    MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");

    assert(menu->isForeground());

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
    if (menu->isForeground()) {
        Tile *ft = userTiles.at(settings->userTiles.focused);
        menu->show(ft);
    }

    // Force-render
    // - redraw all applets
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

// Some applets may be permitted to bring themselves to foreground, to show new data
// User selects which applets have this permission via on-screen menu
// Priority is determined by the order which applets were added to WindowManager in setupNicheGraphics
// We will only autoshow one applet
void InkHUD::WindowManager::autoshow()
{
    // Don't perform autoshow if a system applet has exclusive use of the display right now
    // Note: lockRequests prevents autoshow attempting to hide menuApplet
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->lockRendering || sa->lockRequests)
            return;
    }

    NotificationApplet *notificationApplet = (NotificationApplet *)inkhud->getSystemApplet("Notification");

    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        Applet *a = inkhud->userApplets.at(i);
        if (a->wantsToAutoshow()                  // Applet wants to become foreground
            && !a->isForeground()                 // Not yet foreground
            && settings->userApplets.autoshow[i]) // User permits this applet to autoshow
        {
            Tile *t = userTiles.at(settings->userTiles.focused); // Get focused tile
            t->getAssignedApplet()->sendToBackground();          // Background whichever applet is already on the tile
            t->assignApplet(a);                                  // Assign our new applet to tile
            a->bringToForeground();                              // Foreground our new applet

            // Check if autoshown applet shows the same information as notification intended to
            // In this case, we can dismiss the notification before it is shown
            // Note: we are re-running the approval process. This normally occurs when the notification is initially triggered.
            if (notificationApplet->isForeground() && !notificationApplet->isApproved())
                notificationApplet->dismiss();

            break; // One autoshow only! Avoid conflicts
        }
    }
}

// A collection of any user tiles which do not have a valid user applet
// This can occur in various situations, such as when a user enables fewer applets than their layout has tiles
// The tiles (and which regions the occupy) are private information of the window manager
// The renderer needs to know which regions (if any) are empty,
// in order to fill them with a "placeholder" pattern.
// -- There may be a tidier way to accomplish this --
std::vector<InkHUD::Tile *> InkHUD::WindowManager::getEmptyTiles()
{
    std::vector<Tile *> empty;

    for (Tile *t : userTiles) {
        Applet *a = t->getAssignedApplet();
        if (!a || !a->isActive())
            empty.push_back(t);
    }

    return empty;
}

// Complete the configuration of one newly instantiated system applet
// - link it with its tile
//      Unlike user applets, most system applets have their own unique tile;
//      the only reference to this tile is held by the system applet itself.
// - give it a name
//      A system applet's name is its unique identifier.
//      The name is our only reference to specific system applets, via InkHUD->getSystemApplet
// - add it to the list of system applets

void InkHUD::WindowManager::addSystemApplet(const char *name, SystemApplet *applet, Tile *tile)
{
    // Some system applets might not have their own tile (e.g. menu, placeholder)
    if (tile)
        tile->assignApplet(applet);

    applet->name = name;
    inkhud->systemApplets.push_back(applet);
}

// Create the "system applets"
// These handle things like bootscreen, pop-up notifications etc
// They are processed separately from the user applets, because they might need to do "weird things"
void InkHUD::WindowManager::createSystemApplets()
{
    addSystemApplet("Logo", new LogoApplet, new Tile);
    addSystemApplet("Pairing", new PairingApplet, new Tile);
    addSystemApplet("Tips", new TipsApplet, new Tile);

    addSystemApplet("Menu", new MenuApplet, nullptr);

    // Battery and notifications *behind* the menu
    addSystemApplet("Notification", new NotificationApplet, new Tile);
    addSystemApplet("BatteryIcon", new BatteryIconApplet, new Tile);

    // Special handling only, via Rendering::renderPlaceholders
    addSystemApplet("Placeholder", new PlaceholderApplet, nullptr);

    // System applets are always active
    for (SystemApplet *sa : inkhud->systemApplets)
        sa->activate();
}

// Set the position and size of most system applets
// Most system applets have their own tile. We manually set the region this tile occupies
void InkHUD::WindowManager::placeSystemTiles()
{
    inkhud->getSystemApplet("Logo")->getTile()->setRegion(0, 0, inkhud->width(), inkhud->height());
    inkhud->getSystemApplet("Pairing")->getTile()->setRegion(0, 0, inkhud->width(), inkhud->height());
    inkhud->getSystemApplet("Tips")->getTile()->setRegion(0, 0, inkhud->width(), inkhud->height());

    inkhud->getSystemApplet("Notification")->getTile()->setRegion(0, 0, inkhud->width(), 20);

    const uint16_t batteryIconHeight = Applet::getHeaderHeight() - 2 - 2;
    const uint16_t batteryIconWidth = batteryIconHeight * 1.8;
    inkhud->getSystemApplet("BatteryIcon")
        ->getTile()
        ->setRegion(inkhud->width() - batteryIconWidth, // x
                    2,                                  // y
                    batteryIconWidth,                   // width
                    batteryIconHeight);                 // height

    // Note: the tiles of placeholder and menu applets are manipulated specially
    // - menuApplet borrows user tiles
    // - placeholder applet is temporarily assigned to each user tile of WindowManager::getEmptyTiles
}

// Activate or deactivate user applets, to match settings
// Called at boot, or after run-time config changes via menu
// Note: this method does not instantiate the applets;
// this is done in setupNicheGraphics, when passing to InkHUD::addApplet
void InkHUD::WindowManager::createUserApplets()
{
    // Deactivate and remove any no-longer-needed applets
    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        Applet *a = inkhud->userApplets.at(i);

        // If the applet is active, but settings say it shouldn't be:
        // - run applet's custom deactivation code
        // - mark applet as inactive (internally)
        if (a->isActive() && !settings->userApplets.active[i])
            a->deactivate();
    }

    // Activate and add any new applets
    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {

        // If not activated, but it now should be:
        // - run applet's custom activation code
        // - mark applet as active (internally)
        if (!inkhud->userApplets.at(i)->isActive() && settings->userApplets.active[i])
            inkhud->userApplets.at(i)->activate();
    }
}

// Creates the tiles which will host user applets
// The amount of these is controlled by the user, via "layout" option in the InkHUD menu
void InkHUD::WindowManager::createUserTiles()
{
    // Delete any tiles which currently exist
    for (Tile *t : userTiles)
        delete t;
    userTiles.clear();

    // Create new tiles
    for (uint8_t i = 0; i < settings->userTiles.count; i++) {
        Tile *t = new Tile;
        userTiles.push_back(t);
    }
}

// Calculate the display region occupied by each tile
// This determines how pixels are translated from "relative" applet-space to "absolute" windowmanager-space
// The size and position depend on the amount of tiles the user prefers, set by the "layout" option
void InkHUD::WindowManager::placeUserTiles()
{
    for (uint8_t i = 0; i < userTiles.size(); i++)
        userTiles.at(i)->setRegion(settings->userTiles.count, i);
}

// Link "foreground" user applets with tiles
// Which applet should be *initially* shown on a tile?
// This initial state changes once WindowManager::nextApplet is called.
// Performed at startup, or during certain run-time reconfigurations (e.g number of tiles)
// This state of "which applets are foreground" is preserved between reboots, but the value needs validating at startup.
void InkHUD::WindowManager::assignUserAppletsToTiles()
{
    // Each user tile
    for (uint8_t i = 0; i < userTiles.size(); i++) {
        Tile *t = userTiles.at(i);

        // Check whether tile can display the previously shown applet again
        uint8_t oldIndex = settings->userTiles.displayedUserApplet[i]; // Previous index in WindowManager::userApplets
        bool canRestore = true;
        if (oldIndex > inkhud->userApplets.size() - 1) // Check if old index is now out of bounds
            canRestore = false;
        else if (!settings->userApplets.active[oldIndex]) // Check that old applet is still activated
            canRestore = false;
        else { // Check that the old applet isn't now shown already on a different tile
            for (uint8_t i2 = 0; i2 < i; i2++) {
                if (settings->userTiles.displayedUserApplet[i2] == oldIndex) {
                    canRestore = false;
                    break;
                }
            }
        }

        // Restore previously shown applet if possible,
        // otherwise assign nullptr, which will render specially using placeholderApplet
        if (canRestore) {
            Applet *a = inkhud->userApplets.at(oldIndex);
            t->assignApplet(a);
            a->bringToForeground();
        } else {
            t->assignApplet(nullptr);
            settings->userTiles.displayedUserApplet[i] = -1; // Update settings: current tile has no valid applet
        }
    }
}

// During layout changes, our focused tile setting can become invalid
// This method identifies that situation and corrects for it
void InkHUD::WindowManager::refocusTile()
{
    // Validate "focused tile" setting
    // - info: focused tile responds to button presses: applet cycling, menu, etc
    // - if number of tiles changed, might now be out of index
    if (settings->userTiles.focused >= userTiles.size())
        settings->userTiles.focused = 0;

    // Give "focused tile" a valid applet
    // - scan for another valid applet, which we can addSubstitution
    // - reason: nextApplet() won't cycle if no applet is assigned
    Tile *focusedTile = userTiles.at(settings->userTiles.focused);
    if (!focusedTile->getAssignedApplet()) {
        // Search for available applets
        for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
            Applet *a = inkhud->userApplets.at(i);
            if (a->isActive() && !a->isForeground()) {
                // Found a suitable applet
                // Assign it to the focused tile
                focusedTile->assignApplet(a);
                a->bringToForeground();
                settings->userTiles.displayedUserApplet[settings->userTiles.focused] = i; // Record change: persist after reboot
                break;
            }
        }
    }
}

// Seach for any applets which believe they are foreground, but no longer have a valid tile
// Tidies up after layout changes at runtime
void InkHUD::WindowManager::findOrphanApplets()
{
    for (uint8_t ia = 0; ia < inkhud->userApplets.size(); ia++) {
        Applet *a = inkhud->userApplets.at(ia);

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