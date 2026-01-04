#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Persistence.h"

using namespace NicheGraphics;

// Load settings and latestMessage data
void InkHUD::Persistence::loadSettings()
{
    // Load the InkHUD settings from flash, and check version number
    // We should only consider the version number if the InkHUD flashdata component reports that we *did* actually load flash data
    Settings loadedSettings;
    bool loadSucceeded = FlashData<Settings>::load(&loadedSettings, "settings");
    if (loadSucceeded && loadedSettings.meta.version == SETTINGS_VERSION && loadedSettings.meta.version != 0)
        settings = loadedSettings; // Version matched, replace the defaults with the loaded values
    else
        LOG_WARN("Settings version changed. Using defaults");
}

// Load settings and latestMessage data
void InkHUD::Persistence::loadLatestMessage()
{
    // Load previous "latestMessages" data from flash
    MessageStore store("latest");
    store.loadFromFlash();

    // Place into latestMessage struct, for convenient access
    // Number of strings loaded determines whether last message was broadcast or dm
    if (store.messages.size() == 1) {
        latestMessage.dm = store.messages.at(0);
        latestMessage.wasBroadcast = false;
    } else if (store.messages.size() == 2) {
        latestMessage.dm = store.messages.at(0);
        latestMessage.broadcast = store.messages.at(1);
        latestMessage.wasBroadcast = true;
    }
}

// Save the InkHUD settings to flash
void InkHUD::Persistence::saveSettings()
{
    FlashData<Settings>::save(&settings, "settings");
}

// Save latestMessage data to flash
void InkHUD::Persistence::saveLatestMessage()
{
    // Number of strings saved determines whether last message was broadcast or dm
    MessageStore store("latest");
    store.messages.push_back(latestMessage.dm);
    if (latestMessage.wasBroadcast)
        store.messages.push_back(latestMessage.broadcast);
    store.saveToFlash();
}

/*
void InkHUD::Persistence::printSettings(Settings *settings)
{
    if (SETTINGS_VERSION != 2)
        LOG_WARN("Persistence::printSettings was written for SETTINGS_VERSION=2, current is %d", SETTINGS_VERSION);

    LOG_DEBUG("meta.version=%d", settings->meta.version);
    LOG_DEBUG("userTiles.count=%d", settings->userTiles.count);
    LOG_DEBUG("userTiles.maxCount=%d", settings->userTiles.maxCount);
    LOG_DEBUG("userTiles.focused=%d", settings->userTiles.focused);
    for (uint8_t i = 0; i < MAX_TILES_GLOBAL; i++)
        LOG_DEBUG("userTiles.displayedUserApplet[%d]=%d", i, settings->userTiles.displayedUserApplet[i]);
    for (uint8_t i = 0; i < MAX_USERAPPLETS_GLOBAL; i++)
        LOG_DEBUG("userApplets.active[%d]=%d", i, settings->userApplets.active[i]);
    for (uint8_t i = 0; i < MAX_USERAPPLETS_GLOBAL; i++)
        LOG_DEBUG("userApplets.autoshow[%d]=%d", i, settings->userApplets.autoshow[i]);
    LOG_DEBUG("optionalFeatures.notifications=%d", settings->optionalFeatures.notifications);
    LOG_DEBUG("optionalFeatures.batteryIcon=%d", settings->optionalFeatures.batteryIcon);
    LOG_DEBUG("optionalMenuItems.nextTile=%d", settings->optionalMenuItems.nextTile);
    LOG_DEBUG("optionalMenuItems.backlight=%d", settings->optionalMenuItems.backlight);
    LOG_DEBUG("tips.firstBoot=%d", settings->tips.firstBoot);
    LOG_DEBUG("tips.safeShutdownSeen=%d", settings->tips.safeShutdownSeen);
    LOG_DEBUG("rotation=%d", settings->rotation);
    LOG_DEBUG("recentlyActiveSeconds=%d", settings->recentlyActiveSeconds);
}
*/

#endif