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
    SPI1.begin();

    // Driver
    // -----------------------------

    // Use E-Ink driver
    Drivers::EInk *driver = new Drivers::GDEY0154D67; // Todo: confirm display model
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    // Todo: observe the display's performance in-person and adjust accordingly.
    // Currently set to the values given by Elecrow for EInkDynamicDisplay.
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
    inkhud->persistence->settings.userTiles.maxCount = 2;              // Two applets side-by-side
    inkhud->persistence->settings.rotation = 0;                        // To be confirmed?
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true; // Device definitely has a battery

    // Setup backlight
    // Note: button mapping for this configured further down
    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(PIN_EINK_EN);

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // Inactive
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // Inactive
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // Inactive
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // Inactive
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, no autoshow, default on tile 0

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // As labeled on Elecrow diagram: https://www.elecrow.com/download/product/CIL12901M/ThinkNode-M1_User_Manual.pdf
    constexpr uint8_t PAGE_TURN_BUTTON = 0;
    constexpr uint8_t FUNCTION_BUTTON = 1;

    // Setup the main user button
    buttons->setWiring(PAGE_TURN_BUTTON, PIN_BUTTON2);
    buttons->setTiming(PAGE_TURN_BUTTON, 50, 500); // Todo: confirm 50ms is adequate debounce
    buttons->setHandlerShortPress(PAGE_TURN_BUTTON, []() { InkHUD::InkHUD::getInstance()->shortpress(); });
    buttons->setHandlerLongPress(PAGE_TURN_BUTTON, []() { InkHUD::InkHUD::getInstance()->longpress(); });

    // Setup the aux button
    // Initial testing only: mapped to the backlight
    // Todo: additional features
    buttons->setWiring(FUNCTION_BUTTON, PIN_BUTTON1);
    buttons->setTiming(FUNCTION_BUTTON, 50, 500); // 500ms before latch
    buttons->setHandlerDown(FUNCTION_BUTTON, [backlight]() { backlight->peek(); });
    buttons->setHandlerLongPress(FUNCTION_BUTTON, [backlight]() { backlight->latch(); });
    buttons->setHandlerShortPress(FUNCTION_BUTTON, [backlight]() { backlight->off(); });

    buttons->start();
}

#endif