#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/HINK_E0213A289.h"        // WeAct 2.13"
#include "graphics/niche/Drivers/EInk/HINK_E042A87.h"          // WeAct 4.2"
#include "graphics/niche/Drivers/EInk/ZJY128296_029EAAMFGN.h"  // WeAct 2.9"
#include "graphics/niche/Drivers/EInk/ZJY200200_0154DAAMFGN.h" // WeACt 1.54"

#include "graphics/niche/Inputs/TwoButton.h"

#if !defined(INKHUD_BUILDCONF_DRIVER) || !defined(INKHUD_BUILDCONF_DISPLAYRESILIENCE)
// cppcheck-suppress preprocessorErrorDirective
#error If not using a DIY preset, display model and resilience must be set manually
#endif

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------
    SPI1.begin();

    // Driver
    // -----------------------------

    // Use E-Ink driver
    Drivers::EInk *driver = new Drivers::INKHUD_BUILDCONF_DRIVER;
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update.
    inkhud->setDisplayResilience(INKHUD_BUILDCONF_DISPLAYRESILIENCE); // Suggest roughly ten

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Init settings, and customize defaults
    // Values ignored individually if found saved to flash
    inkhud->persistence->settings.rotation = (driver->height > driver->width ? 1 : 0); // Rotate 90deg to landscape, if needed
    inkhud->persistence->settings.userTiles.maxCount = 4;
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet);
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, false, 3);                       // Default on tile 3
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, false, 2); // Default on tile 2
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true, false, 1);      // Default on tile 1
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet, true, false, 0); // Default on tile 0
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true);                        // Background

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // #0: Main User Button
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    buttons->start();
}

#endif