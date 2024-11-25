#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./settings.h"

using namespace NicheGraphics;

void InkHUD::loadSettingsFromFlash()
{
    FlashData<Settings>::load(&settings, "settings");
}

void InkHUD::saveSettingsToFlash()
{
    FlashData<Settings>::save(&settings, "settings");
}

// Holds InkHUD settings while running
// Saved back to Flash at shutdown
// Accessed by including settings.h
InkHUD::Settings InkHUD::settings;

#endif