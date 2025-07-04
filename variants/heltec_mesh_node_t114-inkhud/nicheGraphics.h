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
    inkhud->persistence->settings.optionalFeatures.notifications = false; // No notifications. Busy mesh.

    // Pick applets
    // Custom selection for OpenSauce
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, false, 0); // Default tile 0
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1), true);
    inkhud->addApplet("Channel 2", new InkHUD::ThreadedMessageApplet(2), true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, true);          // Autoshown if new message
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 1); // Default tile 1
    // Disabled by default
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet);
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    // Background

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