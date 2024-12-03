#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/WindowManager.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/ActiveNodes/ActiveNodesApplet.h"
#include "graphics/niche/InkHUD/Applets/User/LastHeardNodes/LastHeardNodesApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Map/MapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/SingleMessage/SingleMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// #include "graphics/niche/InkHUD/Applets/Examples/BasicExample/BasicExampleApplet.h"
// #include "graphics/niche/InkHUD/Applets/Examples/NewMsgExample/NewMsgExampleApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Inputs/TwoButton.h"
#include "graphics/niche/drivers/Eink/GDEY0154D67.h"

#include "graphics/niche/Fonts/FreeSans6pt7b.h"
#include "graphics/niche/Fonts/FreeSans6pt8bCyrillic.h"
#include <Fonts/FreeSans9pt7b.h>

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // For NRF52 platforms, SPI pins are defined in variant.h, not passed to begin()
    SPIClass *inkSPI = &SPI1;
    inkSPI->begin();

    // Driver
    // -----------------------------

    // Use E-Ink driver
    Drivers::EInk *driver = new Drivers::GDEY0154D67;
    driver->begin(inkSPI, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::WindowManager *windowManager = InkHUD::WindowManager::getInstance();

    // Set the driver
    windowManager->setDriver(driver);

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
    // Values ignored individually if found saved to flash
    InkHUD::settings.userTiles.maxCount = 2; // Two applets side-by-side
    InkHUD::settings.rotation = 3;           // 270 degrees clockwise

    // Pick applets
    windowManager->addApplet("Big Message", new InkHUD::SingleMessageApplet, true);
    windowManager->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true);
    windowManager->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    windowManager->addApplet("Last Heard", new InkHUD::LastHeardNodesApplet, true);
    windowManager->addApplet("Active Nodes", new InkHUD::ActiveNodesApplet);
    // windowManager->addApplet("Map", new InkHUD::MapApplet, true);

    // windowManager->addApplet("Basic", new InkHUD::BasicExampleApplet);
    // windowManager->addApplet("NewMsg", new InkHUD::NewMsgExampleApplet);

    // Start running window manager
    windowManager->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component
    constexpr uint8_t MAIN_BUTTON = 0;
    constexpr uint8_t TOUCH_BUTTON = 1;

    // Setup the main user button
    buttons->setWiring(MAIN_BUTTON, BUTTON_PIN, LOW);
    buttons->setHandlerShortPress(MAIN_BUTTON, []() { InkHUD::WindowManager::getInstance()->handleButtonShort(); });
    buttons->setHandlerLongPress(MAIN_BUTTON, []() { InkHUD::WindowManager::getInstance()->handleButtonLong(); });

    // Setup the capacitive touch button
    buttons->setWiring(TOUCH_BUTTON, PIN_EINK_EN, LOW);
    buttons->setHandlerDown(TOUCH_BUTTON, []() { InkHUD::WindowManager::getInstance()->handleAuxButtonDown(); });
    buttons->setHandlerUp(TOUCH_BUTTON, []() { InkHUD::WindowManager::getInstance()->handleAuxButtonUp(); });

    buttons->start();
}

#endif