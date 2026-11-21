#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Waypoints/WaypointListApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/GDEY0154D67.h"
#include "graphics/niche/Inputs/TwoButton.h"

// Button feedback
#include "buzz.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // Panel is on HSPI
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver = new Drivers::GDEY0154D67;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    // Currently set to the values given by Elecrow for EInkDynamicDisplay.
    inkhud->setDisplayResilience(10, 1.5);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2;              // Two applets side-by-side
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true; // Device definitely has a battery

    // Setup backlight controller
    // LatchingBacklight not used due to backlight being on GPIO expander
    // Note: button is attached further down
    auto backlight = [](bool on) { io.digitalWrite(PCA_PIN_EINK_EN, on ? HIGH : LOW); };
    backlight(false); // start dark

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);      // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                                   // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));             // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));             // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);                // Activated
    inkhud->addApplet("Waypoints", new InkHUD::WaypointListApplet);                   // -
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);                 // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);              // Activated, no autoshow, default on tile 0
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false); // -

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // Elecrow diagram: https://www.elecrow.com/download/product/ILM13205D/ThinkNode_M5_User_Manual.pdf

    // #0: Bottom side button
    // Labeled "Next Page Button" by manual
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin(), true);
    buttons->setTiming(0, 50, 500);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // #1: Top side button
    // Labeled "Previous Page Button" by manual
    // Currently mirrors functionality of LatchingBacklight
    buttons->setWiring(1, PIN_BUTTON2, true);
    buttons->setTiming(1, 50, 500); // 500ms before latch
    buttons->setHandlerDown(1, [backlight]() { backlight(true); });
    buttons->setHandlerLongPress(1, [backlight]() {
        backlight(false);
        delay(25);
        backlight(true);
        delay(25);
        backlight(false);
        delay(25);
        backlight(true);
        playBoop();
    });
    buttons->setHandlerShortPress(1, [backlight]() {
        backlight(false);
        playChirp();
    });

    // Begin handling button events
    buttons->start();
}

#endif
