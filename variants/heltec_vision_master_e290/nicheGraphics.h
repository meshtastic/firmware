/*

Most of the Meshtastic firmware uses preprocessor macros throughout the code to support different hardware variants.
NicheGraphics attempts a different approach:

Per-device config takes place in this setupNicheGraphics() method
(And a small amount in platformio.ini)

This file sets up InkHUD for Heltec VM-E290.
Different NicheGraphics UIs and different hardware variants will each have their own setup procedure.

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
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// #include "graphics/niche/InkHUD/Applets/Examples/BasicExample/BasicExampleApplet.h"
// #include "graphics/niche/InkHUD/Applets/Examples/NewMsgExample/NewMsgExampleApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/DEPG0290BNS800.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "graphics/niche/Fonts/FreeSans6pt7b.h"
#include "graphics/niche/Fonts/FreeSans6pt8bCyrillic.h"
#include <Fonts/FreeSans9pt7b.h>

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

    // Use E-Ink driver
    Drivers::EInk *driver = new Drivers::DEPG0290BNS800;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(7, 1.5);

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
    inkhud->persistence->settings.rotation = 1;           // 90 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalMenuItems.nextTile = false; // Behavior handled by aux button instead

    // Pick applets

    // Order of applets determines priority of "auto-show" feature.
    // Optional arguments for default state:
    //   - is activated?
    //   - is autoshown?
    //   - is foreground on a specific tile (index)?

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

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // A shared NicheGraphics component

    // Setup the main user button (0)
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, []() { InkHUD::InkHUD::getInstance()->shortpress(); });
    buttons->setHandlerLongPress(0, []() { InkHUD::InkHUD::getInstance()->longpress(); });

    // Setup the aux button (1)
    // Bonus feature of VME290
    buttons->setWiring(1, BUTTON_PIN_SECONDARY);
    buttons->setHandlerShortPress(1, []() { InkHUD::InkHUD::getInstance()->nextTile(); });

    buttons->start();
}

#endif