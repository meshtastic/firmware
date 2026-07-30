/*

NicheGraphics setup for Seeed Wio Tracker L1 E-Ink (ZJY122250_0213BAAMFGN 2.13" on SPI1).
Shared by both BaseUI and InkHUD envs.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/ZJY122250_0213BAAMFGN.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "graphics/niche/Applets/Examples/UserAppletInputExample/UserAppletInputExample.h"
#include "graphics/niche/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/Applets/User/DM/DMApplet.h"
#include "graphics/niche/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD.h"
#include "graphics/niche/Inputs/TwoButtonExtended.h"
#endif

#ifdef MESHTASTIC_INCLUDE_INKHUD
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    auto *panel = new Panels::ZJY122250_0213BAAMFGN();
    Drivers::EInk *driver = panel->create();

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(15);
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.rotation = 1;
#if HAS_TRACKBALL
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.alignment = 3;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;
#endif
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;
    inkhud->persistence->settings.userTiles.count = 1;
    inkhud->persistence->settings.userTiles.maxCount = 2;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false);

    inkhud->begin();

    Inputs::TwoButtonExtended *buttons = Inputs::TwoButtonExtended::getInstance();

#if HAS_TRACKBALL
    buttons->setWiring(0, Inputs::TwoButtonExtended::getUserButtonPin());
    buttons->setTiming(0, 75, 500);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->exitShort(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->exitLong(); });

    buttons->setWiring(1, TB_PRESS);
    buttons->setTiming(1, 75, 500);
    buttons->setHandlerShortPress(1, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(1, [inkhud]() { inkhud->longpress(); });

    buttons->setJoystickWiring(TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT);
    buttons->setJoystickDebounce(50);
    buttons->setJoystickPressHandlers([inkhud]() { inkhud->navUp(); }, [inkhud]() { inkhud->navDown(); },
                                      [inkhud]() { inkhud->navLeft(); }, [inkhud]() { inkhud->navRight(); });
#else
    buttons->setWiring(0, Inputs::TwoButtonExtended::getUserButtonPin());
    buttons->setTiming(0, 75, 500);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });
#endif

    buttons->start();
}
#else
void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new NicheGraphics::Panels::ZJY122250_0213BAAMFGN();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(10, 1.5f);
    return display;
}
#endif

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
