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

// Button feedback
#include "buzz.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // For NRF52 platforms, SPI pins are defined in variant.h
    SPI1.begin();

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver = new Drivers::GDEY0154D67;
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    // Todo: observe the display's performance in-person and adjust accordingly.
    // Currently set to the values given by Elecrow for EInkDynamicDisplay.
    inkhud->setDisplayResilience(10, 1.5);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2;                 // Two applets side-by-side
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;    // Device definitely has a battery
    inkhud->persistence->settings.optionalFeatures.notifications = false; // No notifications. Busy mesh.

    // Setup backlight controller
    // Note: button is attached further down
    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(PIN_EINK_EN);

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

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // Elecrow diagram: https://www.elecrow.com/download/product/CIL12901M/ThinkNode-M1_User_Manual.pdf

    // #0: Main User Button
    // Labeled "Page Turn Button" by manual
    buttons->setWiring(0, PIN_BUTTON2);
    buttons->setTiming(0, 50, 500); // Todo: confirm 50ms is adequate debounce
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // #1: Aux Button
    // Labeled "Function Button" by manual
    // Todo: additional features
    buttons->setWiring(1, PIN_BUTTON1);
    buttons->setTiming(1, 50, 500); // 500ms before latch
    buttons->setHandlerDown(1, [backlight]() { backlight->peek(); });
    buttons->setHandlerLongPress(1, [backlight]() {
        backlight->latch();
        playBeep();
    });
    buttons->setHandlerShortPress(1, [backlight]() {
        backlight->off();
        playBoop();
    });

    // Begin handling button events
    buttons->start();
}

#endif