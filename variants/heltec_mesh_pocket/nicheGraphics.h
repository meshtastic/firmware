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

// #include "graphics/niche/InkHUD/Applets/Examples/BasicExample/BasicExampleApplet.h"
// #include "graphics/niche/InkHUD/Applets/Examples/NewMsgExample/NewMsgExampleApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/LCMEN2R13ECC1.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "graphics/niche/Fonts/FreeSans6pt7b.h"
#include "graphics/niche/Fonts/FreeSans6pt8bCyrillic.h"
#include <Fonts/FreeSans9pt7b.h>

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------
    SPIClass *spi1 = &SPI1;
    spi1->begin();
    // Display is connected to SPI1

    // E-Ink Driver
    // -----------------------------
    // Use E-Ink driver
    Drivers::EInk *driver = new Drivers::LCMEN2R13ECC1;
    driver->begin(spi1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(10, 1.5);

    // Prepare fonts
    InkHUD::Applet::fontLarge = InkHUD::AppletFont(FreeSans9pt7b);
    InkHUD::Applet::fontSmall = InkHUD::AppletFont(FreeSans6pt7b);
    /*
    // Font localization demo: Cyrillic
    InkHUD::Applet::fontSmall = InkHUD::AppletFont(FreeSans6pt8bCyrillic);
    InkHUD::Applet::fontSmall.addSubstitutionsWin1251();
    */

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalMenuItems.nextTile = true;

    // Pick applets
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // Inactive
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // Inactive
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // Inactive
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // Inactive
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, not autoshown, default on tile 0
    // inkhud->addApplet("Basic", new InkHUD::BasicExampleApplet);
    // inkhud->addApplet("NewMsg", new InkHUD::NewMsgExampleApplet);

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component
    constexpr uint8_t MAIN_BUTTON = 0;
    // constexpr uint8_t AUX_BUTTON = 1;

    // Setup the main user button
    buttons->setWiring(MAIN_BUTTON, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(MAIN_BUTTON, []() { InkHUD::InkHUD::getInstance()->shortpress(); });
    buttons->setHandlerLongPress(MAIN_BUTTON, []() { InkHUD::InkHUD::getInstance()->longpress(); });

    // Setup the aux button
    // Bonus feature of VME213
    // buttons->setWiring(AUX_BUTTON, BUTTON_PIN_SECONDARY);
    // buttons->setHandlerShortPress(AUX_BUTTON, []() { InkHUD::InkHUD::getInstance()->nextTile(); });
    buttons->start();
}

#endif