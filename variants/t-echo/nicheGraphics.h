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
#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/Drivers/EInk/GDEY0154D67.h"
#include "graphics/niche/Inputs/TwoButton.h"

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

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    windowManager->setDisplayResilience(20, 1.5);

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
    InkHUD::settings.userTiles.maxCount = 2;              // Two applets side-by-side
    InkHUD::settings.rotation = 3;                        // 270 degrees clockwise
    InkHUD::settings.optionalFeatures.batteryIcon = true; // Device definitely has a battery
    InkHUD::settings.optionalMenuItems.backlight = true;  // Until proven (by touch) that user still has the capacitive button

    // Setup backlight
    // Note: AUX button behavior configured further down
    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(PIN_EINK_EN);

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    windowManager->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    windowManager->addApplet("DMs", new InkHUD::DMApplet);
    windowManager->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    windowManager->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    windowManager->addApplet("Positions", new InkHUD::PositionsApplet, true); // Activated
    windowManager->addApplet("Recents List", new InkHUD::RecentsListApplet);
    windowManager->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, no autoshow, default on tile 0
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
    // - short: momentary backlight
    // - long: latch backlight on
    buttons->setWiring(TOUCH_BUTTON, PIN_BUTTON_TOUCH, LOW);
    buttons->setTiming(TOUCH_BUTTON, 50, 5000); // 5 seconds before latch - limited by T-Echo's capacitive touch IC
    buttons->setHandlerDown(TOUCH_BUTTON, [backlight]() {
        backlight->peek();
        InkHUD::settings.optionalMenuItems.backlight = false; // We've proved user still has the button. No need for menu entry.
    });
    buttons->setHandlerLongPress(TOUCH_BUTTON, [backlight]() { backlight->latch(); });
    buttons->setHandlerShortPress(TOUCH_BUTTON, [backlight]() { backlight->off(); });

    buttons->start();
}

#endif