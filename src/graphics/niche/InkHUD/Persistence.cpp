#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Persistence.h"

using namespace NicheGraphics;

// Load settings and latestMessage data
void InkHUD::loadDataFromFlash()
{
    // Load the InkHUD settings from flash, and check version number
    // We should only consider the version number if the InkHUD flashdata component reports that we *did* actually load flash data
    InkHUD::Settings loadedSettings;
    bool loadSucceeded = FlashData<Settings>::load(&loadedSettings, "settings");
    if (loadSucceeded && loadedSettings.meta.version == SETTINGS_VERSION && loadedSettings.meta.version != 0)
        settings = loadedSettings; // Version matched, replace the defaults with the loaded values
    else
        LOG_WARN("Settings version changed. Using defaults");

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

// Save settings and latestMessage data
void InkHUD::saveDataToFlash()
{
    // Save the InkHUD settings to flash
    FlashData<Settings>::save(&settings, "settings");

    // Number of strings saved determines whether last message was broadcast or dm
    MessageStore store("latest");
    store.messages.push_back(latestMessage.dm);
    if (latestMessage.wasBroadcast)
        store.messages.push_back(latestMessage.broadcast);
    store.saveToFlash();
}

// Holds InkHUD settings while running
// Saved back to Flash at shutdown
// Accessed by including persistence.h
InkHUD::Settings InkHUD::settings;

// Holds copies of the most recent broadcast and DM messages while running
// Saved to Flash at shutdown
// Accessed by including persistence.h
InkHUD::LatestMessage InkHUD::latestMessage;

#endif