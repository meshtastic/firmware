#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MenuApplet.h"

#include "RTC.h"

#include "MeshService.h"
#include "airtime.h"
#include "main.h"
#include "power.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

using namespace NicheGraphics;

static constexpr uint8_t MENU_TIMEOUT_SEC = 60; // How many seconds before menu auto-closes

// Options for the "Recents" menu
// These are offered to users as possible values for settings.recentlyActiveSeconds
static constexpr uint8_t RECENTS_OPTIONS_MINUTES[] = {2, 5, 10, 30, 60, 120};

InkHUD::MenuApplet::MenuApplet() : concurrency::OSThread("MenuApplet")
{
    // No timer tasks at boot
    OSThread::disable();

    // Note: don't get instance if we're not actually using the backlight,
    // or else you will unintentionally instantiate it
    if (settings->optionalMenuItems.backlight) {
        backlight = Drivers::LatchingBacklight::getInstance();
    }
}

void InkHUD::MenuApplet::onForeground()
{
    // We do need this before we render, but we can optimize by just calculating it once now
    systemInfoPanelHeight = getSystemInfoPanelHeight();

    // Display initial menu page
    showPage(MenuPage::ROOT);

    // If device has a backlight which isn't controlled by aux button:
    // backlight on always when menu opens.
    // Courtesy to T-Echo users who removed the capacitive touch button
    if (settings->optionalMenuItems.backlight) {
        assert(backlight);
        if (!backlight->isOn())
            backlight->peek();
    }

    // Prevent user applets requesting update while menu is open
    // Handle button input with this applet
    SystemApplet::lockRequests = true;
    SystemApplet::handleInput = true;

    // Begin the auto-close timeout
    OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);
    OSThread::enabled = true;

    // Upgrade the refresh to FAST, for guaranteed responsiveness
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::MenuApplet::onBackground()
{
    // If device has a backlight which isn't controlled by aux button:
    // Item in options submenu allows keeping backlight on after menu is closed
    // If this item is deselected we will turn backlight off again, now that menu is closing
    if (settings->optionalMenuItems.backlight) {
        assert(backlight);
        if (!backlight->isLatched())
            backlight->off();
    }

    // Stop the auto-timeout
    OSThread::disable();

    // Resume normal rendering and button behavior of user applets
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    // Restore the user applet whose tile we borrowed
    if (borrowedTileOwner)
        borrowedTileOwner->bringToForeground();
    Tile *t = getTile();
    t->assignApplet(borrowedTileOwner); // Break our link with the tile, (and relink it with real owner, if it had one)
    borrowedTileOwner = nullptr;

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // We're only updating here to upgrade from UNSPECIFIED to FAST, to ensure responsiveness when exiting menu
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

// Open the menu
// Parameter specifies which user-tile the menu will use
// The user applet originally on this tile will be restored when the menu closes
void InkHUD::MenuApplet::show(Tile *t)
{
    // Remember who *really* owns this tile
    borrowedTileOwner = t->getAssignedApplet();

    // Hide the owner, if it is a valid applet
    if (borrowedTileOwner)
        borrowedTileOwner->sendToBackground();

    // Break the owner's link with tile
    // Relink it to menu applet
    t->assignApplet(this);

    // Show menu
    bringToForeground();
}

// Auto-exit the menu applet after a period of inactivity
// The values shown on the root menu are only a snapshot: they are not re-rendered while the menu remains open.
// By exiting the menu, we prevent users mistakenly believing that the data will update.
int32_t InkHUD::MenuApplet::runOnce()
{
    // runOnce's interval is pushed back when a button is pressed
    // If we do actually run, it means no button input occurred within MENU_TIMEOUT_SEC,
    // so we close the menu.
    showPage(EXIT);

    // Timer should disable after firing
    // This is redundant, as onBackground() will also disable
    return OSThread::disable();
}

// Perform action for a menu item, then change page
// Behaviors for MenuActions are defined here
void InkHUD::MenuApplet::execute(MenuItem item)
{
    // Perform an action
    // ------------------
    switch (item.action) {

    // Open a submenu without performing any action
    // Also handles exit
    case NO_ACTION:
        break;

    case NEXT_TILE:
        inkhud->nextTile();
        break;

    case SEND_PING:
        service->refreshLocalMeshNode();
        service->trySendPosition(NODENUM_BROADCAST, true);

        // Force the next refresh to use FULL, to protect the display, as some users will probably spam this button
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FULL);
        break;

    case ROTATE:
        inkhud->rotate();
        break;

    case LAYOUT:
        // Todo: smarter incrementing of tile count
        settings->userTiles.count++;

        if (settings->userTiles.count == 3) // Skip 3 tiles: not done yet
            settings->userTiles.count++;

        if (settings->userTiles.count > settings->userTiles.maxCount) // Loop around if tile count now too high
            settings->userTiles.count = 1;

        inkhud->updateLayout();
        break;

    case TOGGLE_APPLET:
        settings->userApplets.active[cursor] = !settings->userApplets.active[cursor];
        inkhud->updateAppletSelection();
        break;

    case TOGGLE_AUTOSHOW_APPLET:
        // Toggle settings.userApplets.autoshow[] value, via MenuItem::checkState pointer set in populateAutoshowPage()
        *items.at(cursor).checkState = !(*items.at(cursor).checkState);
        break;

    case TOGGLE_NOTIFICATIONS:
        settings->optionalFeatures.notifications = !settings->optionalFeatures.notifications;
        break;

    case SET_RECENTS:
        // Set value of settings.recentlyActiveSeconds
        // Uses menu cursor to read RECENTS_OPTIONS_MINUTES array (defined at top of this file)
        assert(cursor < sizeof(RECENTS_OPTIONS_MINUTES) / sizeof(RECENTS_OPTIONS_MINUTES[0]));
        settings->recentlyActiveSeconds = RECENTS_OPTIONS_MINUTES[cursor] * 60; // Menu items are in minutes
        break;

    case SHUTDOWN:
        LOG_INFO("Shutting down from menu");
        power->shutdown();
        // Menu is then sent to background via onShutdown
        break;

    case TOGGLE_BATTERY_ICON:
        inkhud->toggleBatteryIcon();
        break;

    case TOGGLE_BACKLIGHT:
        // Note: backlight is already on in this situation
        // We're marking that it should *remain* on once menu closes
        assert(backlight);
        if (backlight->isLatched())
            backlight->off();
        else
            backlight->latch();
        break;

    case TOGGLE_12H_CLOCK:
        config.display.use_12h_clock = !config.display.use_12h_clock;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        break;

    case TOGGLE_GPS:
        gps->toggleGpsMode();
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        break;

    case ENABLE_BLUETOOTH:
        // This helps users recover from a bad wifi config
        LOG_INFO("Enabling Bluetooth");
        config.network.wifi_enabled = false;
        config.bluetooth.enabled = true;
        nodeDB->saveToDisk();
        rebootAtMsec = millis() + 2000;
        break;

    default:
        LOG_WARN("Action not implemented");
    }

    // Move to next page, as defined for the MenuItem
    showPage(item.nextPage);
}

// Display a new page of MenuItems
// May reload same page, or exit menu applet entirely
// Fills the MenuApplet::items vector
void InkHUD::MenuApplet::showPage(MenuPage page)
{
    items.clear();
    items.shrink_to_fit();

    switch (page) {
    case ROOT:
        // Optional: next applet
        if (settings->optionalMenuItems.nextTile && settings->userTiles.count > 1)
            items.push_back(MenuItem("Next Tile", MenuAction::NEXT_TILE, MenuPage::ROOT)); // Only if multiple applets shown

        items.push_back(MenuItem("Send", MenuPage::SEND));
        items.push_back(MenuItem("Options", MenuPage::OPTIONS));
        // items.push_back(MenuItem("Display Off", MenuPage::EXIT)); // TODO
        items.push_back(MenuItem("Save & Shut Down", MenuAction::SHUTDOWN));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case SEND:
        items.push_back(MenuItem("Ping", MenuAction::SEND_PING, MenuPage::EXIT));
        // Todo: canned messages
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case OPTIONS:
        // Optional: backlight
        if (settings->optionalMenuItems.backlight)
            items.push_back(MenuItem(backlight->isLatched() ? "Backlight Off" : "Keep Backlight On", // Label
                                     MenuAction::TOGGLE_BACKLIGHT,                                   // Action
                                     MenuPage::EXIT                                                  // Exit once complete
                                     ));

        // Optional: GPS
        if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED)
            items.push_back(MenuItem("Enable GPS", MenuAction::TOGGLE_GPS, MenuPage::EXIT));
        if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED)
            items.push_back(MenuItem("Disable GPS", MenuAction::TOGGLE_GPS, MenuPage::EXIT));

        // Optional: Enable Bluetooth, in case of lost wifi connection
        if (!config.bluetooth.enabled || config.network.wifi_enabled)
            items.push_back(MenuItem("Enable Bluetooth", MenuAction::ENABLE_BLUETOOTH, MenuPage::EXIT));

        items.push_back(MenuItem("Applets", MenuPage::APPLETS));
        items.push_back(MenuItem("Auto-show", MenuPage::AUTOSHOW));
        items.push_back(MenuItem("Recents Duration", MenuPage::RECENTS));
        if (settings->userTiles.maxCount > 1)
            items.push_back(MenuItem("Layout", MenuAction::LAYOUT, MenuPage::OPTIONS));
        items.push_back(MenuItem("Rotate", MenuAction::ROTATE, MenuPage::OPTIONS));
        items.push_back(MenuItem("Notifications", MenuAction::TOGGLE_NOTIFICATIONS, MenuPage::OPTIONS,
                                 &settings->optionalFeatures.notifications));
        items.push_back(MenuItem("Battery Icon", MenuAction::TOGGLE_BATTERY_ICON, MenuPage::OPTIONS,
                                 &settings->optionalFeatures.batteryIcon));
        items.push_back(
            MenuItem("12-Hour Clock", MenuAction::TOGGLE_12H_CLOCK, MenuPage::OPTIONS, &config.display.use_12h_clock));
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case APPLETS:
        populateAppletPage();
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case AUTOSHOW:
        populateAutoshowPage();
        items.push_back(MenuItem("Exit", MenuPage::EXIT));
        break;

    case RECENTS:
        populateRecentsPage();
        break;

    case EXIT:
        sendToBackground(); // Menu applet dismissed, allow normal behavior to resume
        break;

    default:
        LOG_WARN("Page not implemented");
    }

    // Reset the cursor, unless reloading same page
    // (or now out-of-bounds)
    if (page != currentPage || cursor >= items.size()) {
        cursor = 0;

        // ROOT menu has special handling: unselected at first, to emphasise the system info panel
        if (page == ROOT)
            cursorShown = false;
    }

    // Remember which page we are on now
    currentPage = page;
}

void InkHUD::MenuApplet::onRender()
{
    if (items.size() == 0)
        LOG_ERROR("Empty Menu");

    // Dimensions for the slots where we will draw menuItems
    const float padding = 0.05;
    const uint16_t itemH = fontSmall.lineHeight() * 2;
    const int16_t itemW = width() - X(padding) - X(padding);
    const int16_t itemL = X(padding);
    const int16_t itemR = X(1 - padding);
    int16_t itemT = 0; // Top (y px of current slot). Incremented as we draw. Adjusted to fit system info panel on ROOT menu.

    // How many full menuItems will fit on screen
    uint8_t slotCount = (height() - itemT) / itemH;

    // System info panel at the top of the menu
    // =========================================

    uint16_t &siH = systemInfoPanelHeight;                   // System info - height. Calculated at onForeground
    const uint8_t slotsObscured = ceilf(siH / (float)itemH); // How many slots are obscured by system info panel

    // System info - top
    // Remain at 0px, until cursor reaches bottom of screen, then begin to scroll off screen.
    // This is the same behavior we expect from the non-root menus.
    // Implementing this with the systemp panel is slightly annoying though,
    // and required adding the MenuApplet::getSystemInfoPanelHeight method
    int16_t siT;
    if (cursor < slotCount - slotsObscured - 1) // (Minus 1: comparing zero based index with a count)
        siT = 0;
    else
        siT = 0 - ((cursor - (slotCount - slotsObscured - 1)) * itemH);

    // If showing ROOT menu,
    // and the panel isn't yet scrolled off screen top
    if (currentPage == ROOT) {
        drawSystemInfoPanel(0, siT, width()); // Draw the panel.
        itemT = max(siT + siH, 0);            // Offset the first menu entry, so menu starts below the system info panel
    }

    // Draw menu items
    // ===================

    // Which item will be drawn to the top-most slot?
    // Initially, this is the item 0, but may increase once we begin scrolling
    uint8_t firstItem;
    if (cursor < slotCount)
        firstItem = 0;
    else
        firstItem = cursor - (slotCount - 1);

    // Which item will be drawn to the bottom-most slot?
    // This may be beyond the slot-count, to draw a partially off-screen item below the bottom-most slow
    // This may be less than the slot-count, if we are reaching the end of the menuItems
    uint8_t lastItem = min((uint8_t)firstItem + slotCount, (uint8_t)items.size() - 1);

    // -- Loop: draw each (visible) menu item --
    for (uint8_t i = firstItem; i <= lastItem; i++) {
        // Grab the menuItem
        MenuItem item = items.at(i);

        // Center-line for the text
        int16_t center = itemT + (itemH / 2);

        // Box, if currently selected
        if (cursorShown && i == cursor)
            drawRect(itemL, itemT, itemW, itemH, BLACK);

        // Item's text
        printAt(itemL + X(padding), center, item.label, LEFT, MIDDLE);

        // Checkbox, if relevant
        if (item.checkState) {
            const uint16_t cbWH = fontSmall.lineHeight();  // Checkbox: width / height
            const int16_t cbL = itemR - X(padding) - cbWH; // Checkbox: left
            const int16_t cbT = center - (cbWH / 2);       // Checkbox : top
            // Checkbox ticked
            if (*(item.checkState)) {
                drawRect(cbL, cbT, cbWH, cbWH, BLACK);
                // First point of tick: pen down
                const int16_t t1Y = center;
                const int16_t t1X = cbL + 3;
                // Second point of tick: base
                const int16_t t2Y = center + (cbWH / 2) - 2;
                const int16_t t2X = cbL + (cbWH / 2);
                // Third point of tick: end of tail
                const int16_t t3Y = center - (cbWH / 2) - 2;
                const int16_t t3X = cbL + cbWH + 2;
                // Draw twice: faux bold
                drawLine(t1X, t1Y, t2X, t2Y, BLACK);
                drawLine(t2X, t2Y, t3X, t3Y, BLACK);
                drawLine(t1X + 1, t1Y, t2X + 1, t2Y, BLACK);
                drawLine(t2X + 1, t2Y, t3X + 1, t3Y, BLACK);
            }
            // Checkbox ticked
            else
                drawRect(cbL, cbT, cbWH, cbWH, BLACK);
        }

        // Increment the y value (top) as we go
        itemT += itemH;
    }
}

void InkHUD::MenuApplet::onButtonShortPress()
{
    // Push the auto-close timer back
    OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

    // Move menu cursor to next entry, then update
    if (cursorShown)
        cursor = (cursor + 1) % items.size();
    else
        cursorShown = true;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::MenuApplet::onButtonLongPress()
{
    // Push the auto-close timer back
    OSThread::setIntervalFromNow(MENU_TIMEOUT_SEC * 1000UL);

    if (cursorShown)
        execute(items.at(cursor));
    else
        showPage(MenuPage::EXIT); // Special case: Peek at root-menu; longpress again to close

    // If we didn't already request a specialized update, when handling a menu action,
    // then perform the usual fast update.
    // FAST keeps things responsive: important because we're dealing with user input
    if (!wantsToRender())
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Dynamically create MenuItem entries for activating / deactivating Applets, for the "Applet Selection" submenu
void InkHUD::MenuApplet::populateAppletPage()
{
    assert(items.size() == 0);

    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        const char *name = inkhud->userApplets.at(i)->name;
        bool *isActive = &(settings->userApplets.active[i]);
        items.push_back(MenuItem(name, MenuAction::TOGGLE_APPLET, MenuPage::APPLETS, isActive));
    }
}

// Dynamically create MenuItem entries for selecting which applets will automatically come to foreground when they have new data
// We only populate this menu page with applets which are actually active
// We use the MenuItem::checkState pointer to toggle the setting in MenuApplet::execute. Bit of a hack, but convenient.
void InkHUD::MenuApplet::populateAutoshowPage()
{
    assert(items.size() == 0);

    for (uint8_t i = 0; i < inkhud->userApplets.size(); i++) {
        // Only add a menu item if applet is active
        if (settings->userApplets.active[i]) {
            const char *name = inkhud->userApplets.at(i)->name;
            bool *isActive = &(settings->userApplets.autoshow[i]);
            items.push_back(MenuItem(name, MenuAction::TOGGLE_AUTOSHOW_APPLET, MenuPage::AUTOSHOW, isActive));
        }
    }
}

void InkHUD::MenuApplet::populateRecentsPage()
{
    // How many values are shown for use to choose from
    constexpr uint8_t optionCount = sizeof(RECENTS_OPTIONS_MINUTES) / sizeof(RECENTS_OPTIONS_MINUTES[0]);

    // Create an entry for each item in RECENTS_OPTIONS_MINUTES array
    // (Defined at top of this file)
    for (uint8_t i = 0; i < optionCount; i++) {
        std::string label = to_string(RECENTS_OPTIONS_MINUTES[i]) + " mins";
        items.push_back(MenuItem(label.c_str(), MenuAction::SET_RECENTS, MenuPage::EXIT));
    }
}

// Renders the panel shown at the top of the root menu.
// Displays the clock, and several other pieces of instantaneous system info,
// which we'd prefer not to have displayed in a normal applet, as they update too frequently.
void InkHUD::MenuApplet::drawSystemInfoPanel(int16_t left, int16_t top, uint16_t width, uint16_t *renderedHeight)
{
    // Reset the height
    // We'll add to this as we add elements
    uint16_t height = 0;

    // Clock (potentially)
    // ====================
    std::string clockString = getTimeString();
    if (clockString.length() > 0) {
        setFont(fontLarge);
        printAt(width / 2, top, clockString, CENTER, TOP);

        height += fontLarge.lineHeight();
        height += fontLarge.lineHeight() * 0.1; // Padding below clock
    }

    // Stats
    // ===================

    setFont(fontSmall);

    // Position of the label row for the system info
    const int16_t labelT = top + height;
    height += fontSmall.lineHeight() * 1.1; // Slightly increased spacing

    // Position of the data row for the system info
    const int16_t valT = top + height;
    height += fontSmall.lineHeight() * 1.1; // Slightly increased spacing (between bottom line and divider)

    // Position of divider between the info panel and the menu entries
    const int16_t divY = top + height;
    height += fontSmall.lineHeight() * 0.2; // Padding *below* the divider. (Above first menu item)

    // Create a variable number of columns
    // Either 3 or 4, depending on whether we have GPS
    // Todo
    constexpr uint8_t N_COL = 3;
    int16_t colL[N_COL];
    int16_t colC[N_COL];
    int16_t colR[N_COL];
    for (uint8_t i = 0; i < N_COL; i++) {
        colL[i] = left + ((width / N_COL) * i);
        colC[i] = colL[i] + ((width / N_COL) / 2);
        colR[i] = colL[i] + (width / N_COL);
    }

    // Info blocks, left to right

    // Voltage
    float voltage = powerStatus->getBatteryVoltageMv() / 1000.0;
    char voltageStr[6]; // "XX.XV"
    sprintf(voltageStr, "%.1fV", voltage);
    printAt(colC[0], labelT, "Bat", CENTER, TOP);
    printAt(colC[0], valT, voltageStr, CENTER, TOP);

    // Divider
    for (int16_t y = valT; y <= divY; y += 3)
        drawPixel(colR[0], y, BLACK);

    // Channel Util
    char chUtilStr[4]; // "XX%"
    sprintf(chUtilStr, "%2.f%%", airTime->channelUtilizationPercent());
    printAt(colC[1], labelT, "Ch", CENTER, TOP);
    printAt(colC[1], valT, chUtilStr, CENTER, TOP);

    // Divider
    for (int16_t y = valT; y <= divY; y += 3)
        drawPixel(colR[1], y, BLACK);

    // Duty Cycle (AirTimeTx)
    char dutyUtilStr[4]; // "XX%"
    sprintf(dutyUtilStr, "%2.f%%", airTime->utilizationTXPercent());
    printAt(colC[2], labelT, "Duty", CENTER, TOP);
    printAt(colC[2], valT, dutyUtilStr, CENTER, TOP);

    /*
    // Divider
    for (int16_t y = valT; y <= divY; y += 3)
        drawPixel(colR[2], y, BLACK);

    // GPS satellites - todo
    printAt(colC[3], labelT, "Sats", CENTER, TOP);
    printAt(colC[3], valT, "ToDo", CENTER, TOP);
    */

    // Horizontal divider, at bottom of system info panel
    for (int16_t x = 0; x < width; x += 2) // Divider, centered in the padding between first system panel and first item
        drawPixel(x, divY, BLACK);

    if (renderedHeight != nullptr)
        *renderedHeight = height;
}

// Get the height of the the panel drawn at the top of the menu
// This is inefficient, as we do actually have to render the panel to determine the height
// It solves a catch-22 situation, where slotCount needs to know panel height, and panel height needs to know slotCount
uint16_t InkHUD::MenuApplet::getSystemInfoPanelHeight()
{
    // Render *far* off screen
    uint16_t height = 0;
    drawSystemInfoPanel(INT16_MIN, INT16_MIN, 1, &height);

    return height;
}

#endif