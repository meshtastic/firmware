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

namespace NicheGraphics::InkHUD
{

constexpr uint8_t MAX_TILES_GLOBAL = 4;
constexpr uint8_t MAX_USERAPPLETS_GLOBAL = 16;

struct Settings {
    struct Meta {
        // Used to invalidate old savefiles, if we make breaking changes
        uint32_t version = -1;
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

    struct OptionalFeatures {
        bool notifications = true;
        bool batteryIcon = false;
    } optionalFeatures;

    // Most recently received text message
    // Value is updated by InkHUD::WindowManager, as a courtesty to applets
    // Note: different from devicestate.rx_text_message,
    // which may contain an *outgoing message* to broadcast
    struct LastMessage {
        uint32_t nodeNum = 0;     // Who from
        uint32_t timestamp = 0;   // When (epoch seconds)
        uint8_t channelIndex = 0; // Received on which channel
        char text[255]{0};
    } lastMessage;

    uint8_t rotation = 1;

    // How long do we consider another node to be "active"?
    // Used when applets want to filter for "active nodes" only
    uint32_t recentlyActiveSeconds = 2 * 60;
};

extern Settings settings;

void loadSettingsFromFlash();
void saveSettingsToFlash();

} // namespace NicheGraphics::InkHUD

#endif