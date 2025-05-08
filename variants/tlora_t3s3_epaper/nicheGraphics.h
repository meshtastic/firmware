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
#include "graphics/niche/Drivers/EInk/DEPG0213BNS800.h"
#include "graphics/niche/Inputs/TwoButton.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // Display is connected to HSPI
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver = new Drivers::DEPG0213BNS800;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(15, 1.5);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, not autoshown, default on tile 0

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // Setup the main user button
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin(), true);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    buttons->start();
}

#endif