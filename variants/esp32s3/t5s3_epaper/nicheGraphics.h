/*

Most of the Meshtastic firmware uses preprocessor macros throughout the code to support different hardware variants.
NicheGraphics attempts a different approach:

Per-device config takes place in this setupNicheGraphics() method
(And a small amount in platformio.ini)

This file sets up InkHUD for the LilyGo T5-E-Paper-S3-Pro.

The board uses a 4.7" ED047TC1 parallel e-paper display (960×540, 8-bit parallel interface).
This is driven via the FastEPD library through the NicheGraphics ED047TC1 driver adapter.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/Drivers/EInk/ED047TC1.h"
#include "graphics/niche/Inputs/TwoButton.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // E-Ink Driver
    // -----------------------------
    // The ED047TC1 is a parallel display — no SPI bus setup needed.
    // begin() args are part of the EInk interface but are ignored for parallel displays.

    Drivers::EInk *driver = new Drivers::ED047TC1;
    driver->begin(nullptr, 0, 0, 0);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(7, 1.5);

    // Prepare fonts — use larger sizes to suit the 4.7" screen at ~234 DPI
    InkHUD::Applet::fontLarge = FREESANS_24PT_WIN1253;
    InkHUD::Applet::fontMedium = FREESANS_18PT_WIN1253;
    InkHUD::Applet::fontSmall = FREESANS_12PT_WIN1253;

    // Load persisted settings. begin() calls loadSettings() internally, so we save after
    // applying defaults to ensure begin() picks up our values from flash.
    inkhud->persistence->loadSettings();
    if (inkhud->persistence->settings.tips.firstBoot) {
        inkhud->persistence->settings.rotation = 3;
        inkhud->persistence->settings.userTiles.maxCount = 2;
        inkhud->persistence->settings.userTiles.count = 1;
        inkhud->persistence->settings.optionalFeatures.batteryIcon = true;
        inkhud->persistence->settings.optionalMenuItems.backlight = true;
    }
    // Alignment must cancel rotation for visual-frame touch input: (rotation + alignment) % 4 == 0.
    // Recomputed on every boot so it tracks persisted rotation. The bridge also updates it at runtime.
    inkhud->persistence->settings.joystick.alignment = (4 - inkhud->persistence->settings.rotation) % 4;
    inkhud->persistence->saveSettings();

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    // Optional arguments for defaults:
    // - is activated?
    // - is autoshown?
    // - is foreground on a specific tile (index)?
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);      // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, false);                      // Activated, not autoshown
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, true); // Activated, autoshown
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true); // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, not autoshown, default on tile 0
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);

    // Backlight
    // ----------------------------
    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(BOARD_BL_EN); // GPIO11 on V2

    // Start running InkHUD
    inkhud->begin();

    // Touch navigation requires joystick mode — enforce post-begin so flash cannot override.
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.aligned = true;

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // A shared NicheGraphics component

    // Setup the main user button (boot button, GPIO 0)
    buttons->setWiring(0, BUTTON_PIN);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // No dedicated aux button on this board

    buttons->start();
}

#endif