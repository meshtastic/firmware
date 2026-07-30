/*

NicheGraphics setup for Elecrow ThinkNode-M1 (GDEY0154D67 1.54" on SPI1).
Shared by both BaseUI and InkHUD envs.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Backlight/LatchingBacklight.h"
#include "graphics/eink/Panels/GDEY0154D67.h"

#include "buzz.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "graphics/niche/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/Applets/User/DM/DMApplet.h"
#include "graphics/niche/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD.h"
#include "graphics/niche/Inputs/TwoButton.h"
#endif

#ifdef MESHTASTIC_INCLUDE_INKHUD
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    auto *panel = new Panels::GDEY0154D67();
    Drivers::EInk *driver = panel->create();

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(10, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;

    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(PIN_EINK_EN);

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);
    inkhud->begin();

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance();
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });
#ifdef PIN_BUTTON2
    buttons->setWiring(1, PIN_BUTTON2);
    buttons->setHandlerShortPress(1, [backlight]() { backlight->latch(); });
    buttons->setHandlerLongPress(1, [backlight]() { backlight->off(); });
#endif
    buttons->start();
}
#else
void setupNicheGraphics()
{
    // Backlight on by default for the original BaseUI build.
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, HIGH);
}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new NicheGraphics::Panels::GDEY0154D67();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(10, 1.5f);
    return display;
}
#endif

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
