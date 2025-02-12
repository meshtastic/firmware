#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/WindowManager.h"

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
#include "graphics/niche/Drivers/EInk/LCMEN2R13EFC1.h"
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
    Drivers::EInk *driver = new Drivers::LCMEN213EFC1;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::WindowManager *windowManager = InkHUD::WindowManager::getInstance();

    // Set the driver
    windowManager->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    windowManager->setDisplayResilience(10, 1.5);

    // Prepare fonts
    InkHUD::AppletFont largeFont(FreeSans9pt7b);
    InkHUD::AppletFont smallFont(FreeSans6pt7b);
    /*
    // Font localization demo: Cyrillic
    InkHUD::AppletFont smallFont(FreeSans6pt8bCyrillic);
    smallFont.addSubstitutionsWin1251();
    */
    InkHUD::Applet::setDefaultFonts(largeFont, smallFont);

    // Init settings, and customize defaults
    InkHUD::settings.userTiles.maxCount = 2;             // How many tiles can the display handle?
    InkHUD::settings.rotation = 3;                       // 270 degrees clockwise
    InkHUD::settings.userTiles.count = 1;                // One tile only by default, keep things simple for new users
    InkHUD::settings.optionalMenuItems.nextTile = false; // Behavior handled by aux button instead

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    // Optional arguments for defaults:
    // - is activated?
    // - is autoshown?
    // - is foreground on a specific tile (index)?
    windowManager->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    windowManager->addApplet("DMs", new InkHUD::DMApplet);
    windowManager->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    windowManager->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    windowManager->addApplet("Positions", new InkHUD::PositionsApplet, true); // Activated
    windowManager->addApplet("Recents List", new InkHUD::RecentsListApplet);
    windowManager->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, not autoshown, default on tile 0
    // windowManager->addApplet("Basic", new InkHUD::BasicExampleApplet);
    // windowManager->addApplet("NewMsg", new InkHUD::NewMsgExampleApplet);

    // Start running window manager
    windowManager->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component
    constexpr uint8_t MAIN_BUTTON = 0;
    constexpr uint8_t AUX_BUTTON = 1;

    // Setup the main user button
    buttons->setWiring(MAIN_BUTTON, BUTTON_PIN);
    buttons->setHandlerShortPress(MAIN_BUTTON, []() { InkHUD::WindowManager::getInstance()->handleButtonShort(); });
    buttons->setHandlerLongPress(MAIN_BUTTON, []() { InkHUD::WindowManager::getInstance()->handleButtonLong(); });

    // Setup the aux button
    // Bonus feature of VME213
    buttons->setWiring(AUX_BUTTON, BUTTON_PIN_SECONDARY);
    buttons->setHandlerShortPress(AUX_BUTTON, []() { InkHUD::WindowManager::getInstance()->nextTile(); });
    buttons->start();
}

#endif