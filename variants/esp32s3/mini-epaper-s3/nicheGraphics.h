#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

// Shared NicheGraphics components
#include "graphics/niche/Drivers/EInk/GDEW0102T4.h"
#include "graphics/niche/Inputs/TwoButtonExtended.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // Power-enable the E-Ink panel on this board before any SPI traffic.
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, HIGH);
    delay(10);

    // Display uses HSPI on this board
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    Drivers::GDEW0102T4 *displayDriver = new Drivers::GDEW0102T4;
    displayDriver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);
    // Tuned fast-refresh values reg30 reg50 reg82 lutW2 lutB2 = 11 F2 04 11 0D
    displayDriver->setFastConfig({0x11, 0xF2, 0x04, 0x11, 0x0D});
    Drivers::EInk *driver = displayDriver;

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    // Slightly stricter FAST/FULL
    inkhud->setDisplayResilience(5, 1.5);
    inkhud->twoWayRocker = true;

    // Fonts
    InkHUD::Applet::fontLarge = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_6PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Small display defaults
    inkhud->persistence->settings.rotation = 0;
    inkhud->persistence->settings.userTiles.maxCount = 1;
    inkhud->persistence->settings.userTiles.count = 1;
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.aligned = true;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, false, false);      // -
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, false);                        // Activated, not autoshown
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, true);   // Activated, Autoshown
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1), false, false); // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);                  // Activated
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false);   // -
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet, false, false);     // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, not autoshown, default on tile 0
    // Start running InkHUD
    inkhud->begin();

    // Enforce two-way rocker behavior regardless of persisted settings.
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.aligned = true;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    // Inputs
    Inputs::TwoButtonExtended *buttons = Inputs::TwoButtonExtended::getInstance();

    // Center press (boot button)
    buttons->setWiring(0, INPUTDRIVER_TWO_WAY_ROCKER_BTN, true);
    // Match baseUI encoder long-press feel.
    buttons->setTiming(0, 75, 300);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // LEFT rocker pin is IO4; RIGHT rocker pin is IO3.
    buttons->setTwoWayRockerWiring(INPUTDRIVER_TWO_WAY_ROCKER_LEFT, INPUTDRIVER_TWO_WAY_ROCKER_RIGHT, true);
    buttons->setJoystickDebounce(50);

    // Two-way rocker behavior:
    // - when a system applet is handling input (menu, tips, etc): LEFT=up, RIGHT=down
    // - otherwise: LEFT=previous applet, RIGHT=next applet
    buttons->setTwoWayRockerPressHandlers(
        [inkhud]() {
            bool systemHandlingInput = false;
            for (InkHUD::SystemApplet *sa : inkhud->systemApplets) {
                if (sa->handleInput) {
                    systemHandlingInput = true;
                    break;
                }
            }

            if (systemHandlingInput)
                inkhud->navUp();
            else
                inkhud->prevApplet();
        },
        [inkhud]() {
            bool systemHandlingInput = false;
            for (InkHUD::SystemApplet *sa : inkhud->systemApplets) {
                if (sa->handleInput) {
                    systemHandlingInput = true;
                    break;
                }
            }

            if (systemHandlingInput)
                inkhud->navDown();
            else
                inkhud->nextApplet();
        });

    buttons->start();
}

#endif
