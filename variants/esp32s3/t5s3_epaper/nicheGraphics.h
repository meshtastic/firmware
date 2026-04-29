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

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;
    inkhud->persistence->settings.optionalMenuItems.backlight = false;

    // Alignment must cancel rotation for visual-frame touch input: (rotation + alignment) % 4 == 0.
    inkhud->persistence->settings.joystick.alignment = (4 - inkhud->persistence->settings.rotation) % 4;

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, false, false);      // Not Active, not autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, true);                         // Activated, Autoshown
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, true);   // Activated, Autoshown
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1), false, false); // Not Active, not autoshown
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true, false);           // Activated, not autoshown
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet, true, false);      // Activated, not autoshown
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, not autoshown, default on tile 0
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false); // Not Active, not autoshown

    // Enable reusable InkHUD touch status indicator for this touch-capable board.
    inkhud->setTouchEnabledProvider(isTouchInputEnabled);

    // Start running InkHUD
    inkhud->begin();
    // Arm GT911 capacitive-home callback only after InkHUD startup is complete.
    t5SetHomeCapButtonEventsEnabled(true);

    // Keep Wireless Paper single-button semantics regardless of persisted settings:
    // short press advances, long press opens menu/selects.
    inkhud->persistence->settings.joystick.enabled = false;

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // A shared NicheGraphics component

    // #0: BOOT button (primary user input for InkHUD navigation on T5-S3)
#if defined(T5_S3_EPAPER_PRO_V1)
    buttons->setWiring(0, PIN_BUTTON2);
#else
    buttons->setWiring(0, BUTTON_PIN);
#endif
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    buttons->start();
}

#endif
