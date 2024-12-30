#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

A quick and dirty alternative to storing "device only" settings using the protobufs
Convenient during development.
Potentially a polite option, to avoid polluting the generated code with values for obscure use cases like this.

The save / load mechanism is a shared NicheGraphics feature.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/FlashData.h"
#include "graphics/niche/InkHUD/MessageStore.h"

namespace NicheGraphics::InkHUD
{

constexpr uint8_t MAX_TILES_GLOBAL = 4;
constexpr uint8_t MAX_USERAPPLETS_GLOBAL = 16;

// Used to invalidate old settings, if needed
// Version 0 is reserved for testing, and will always load defaults
constexpr uint32_t SETTINGS_VERSION = 1;

struct Settings {
    struct Meta {
        // Used to invalidate old savefiles, if we make breaking changes
        uint32_t version = SETTINGS_VERSION;
    } meta;

    struct UserTiles {
        // How many tiles are shown
        uint8_t count = 1;

        // Maximum amount of tiles for this display
        uint8_t maxCount = 4;

        // Which tile is focused (responding to user button input)
        uint8_t focused = 0;

        // Which applet is displayed on which tile
        // Index of array: which tile, as indexed in WindowManager::tiles
        // Value of array: which applet, as indexed in WindowManager::activeApplets
        uint8_t displayedUserApplet[MAX_TILES_GLOBAL] = {0, 1, 2, 3};
    } userTiles;

    struct UserApplets {
        // Which applets are running (either displayed, or in the background)
        // Index of array: which applet, as indexed in WindowManager::applets
        // Initial value is set by the "activeByDefault" parameter of WindowManager::addApplet, in setupNicheGraphics()
        bool active[MAX_USERAPPLETS_GLOBAL];

        // Which user applets should be automatically shown when they have important data to show
        // If none set, foreground applets should remain foreground without manual user input
        // If multiple applets request this at once,
        // priority is the order which they were passed to WindowManager::addApplets, in setupNicheGraphics()
        bool autoshow[MAX_USERAPPLETS_GLOBAL]{false};
    } userApplets;

    // Features which the use can enable / disable via the on-screen menu
    struct OptionalFeatures {
        bool notifications = true;
        bool batteryIcon = false;
    } optionalFeatures;

    // Some menu items may not be required, based on device / configuration
    // We can enable them only when needed, to de-clutter the menu
    struct OptionalMenuItems {
        // If aux button is used to swap between tiles, we have to need for this menu item
        bool nextTile = true;

        // Used if backlight present, and not controlled by AUX button
        // If this item is added to menu: backlight is always active when menu is open
        // The added menu items then allows the user to "Keep Backlight On", globally.
        bool backlight = false;
    } optionalMenuItems;

    // Rotation of the display
    // Multiples of 90 degrees clockwise
    // Most commonly: rotation is 0 when flex connector is oriented below display
    uint8_t rotation = 1;

    // How long do we consider another node to be "active"?
    // Used when applets want to filter for "active nodes" only
    uint32_t recentlyActiveSeconds = 2 * 60;
};

// Most recently received text message
// Value is updated by InkHUD::WindowManager, as a courtesty to applets
// Note: different from devicestate.rx_text_message,
// which may contain an *outgoing message* to broadcast
struct LatestMessage {
    MessageStore::Message broadcast; // Most recent message received broadcast
    MessageStore::Message dm;        // Most recent received DM
    bool wasBroadcast;               // True if most recent broadcast is newer than most recent dm
};

extern Settings settings;
extern LatestMessage latestMessage;

void loadDataFromFlash();
void saveDataToFlash();

} // namespace NicheGraphics::InkHUD

#endif