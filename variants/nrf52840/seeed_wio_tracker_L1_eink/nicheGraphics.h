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
#include "graphics/niche/Drivers/EInk/ZJY122250_0213BAAMFGN.h"
#include "graphics/niche/Inputs/TwoButtonExtended.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // For NRF52 platforms, SPI pins are defined in variant.h
    SPI1.begin();

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver = new Drivers::ZJY122250_0213BAAMFGN;
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    inkhud->setDisplayResilience(15);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.rotation = 1; // 90 degrees clockwise
#if HAS_TRACKBALL
    inkhud->persistence->settings.joystick.enabled = true;            // Device uses a joystick
    inkhud->persistence->settings.joystick.alignment = 3;             // 270 degrees
    inkhud->persistence->settings.optionalMenuItems.nextTile = false; // Use joystick instead
#endif
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true; // Device definitely has a battery
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.userTiles.maxCount = 2; // Two applets side-by-side

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, no autoshow, default on tile 0

    //  Start running InkHUD
    inkhud->begin();

    //  Buttons
    //  --------------------------

    Inputs::TwoButtonExtended *buttons = Inputs::TwoButtonExtended::getInstance(); // Shared NicheGraphics component

#if HAS_TRACKBALL
    // #0: Exit Button
    buttons->setWiring(0, Inputs::TwoButtonExtended::getUserButtonPin());
    buttons->setTiming(0, 75, 500);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->exitShort(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->exitLong(); });

    // #1: Joystick Center
    buttons->setWiring(1, TB_PRESS);
    buttons->setTiming(1, 75, 500);
    buttons->setHandlerShortPress(1, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(1, [inkhud]() { inkhud->longpress(); });

    // Joystick Directions
    buttons->setJoystickWiring(TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT);
    buttons->setJoystickDebounce(50);
    buttons->setJoystickPressHandlers([inkhud]() { inkhud->navUp(); }, [inkhud]() { inkhud->navDown(); },
                                      [inkhud]() { inkhud->navLeft(); }, [inkhud]() { inkhud->navRight(); });
#else
    // #0: User Button
    buttons->setWiring(0, Inputs::TwoButtonExtended::getUserButtonPin());
    buttons->setTiming(0, 75, 500);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });
#endif

    // Begin handling button events
    buttons->start();
}

#endif
