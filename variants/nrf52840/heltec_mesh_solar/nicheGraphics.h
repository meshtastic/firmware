/*

NicheGraphics setup for Heltec MeshSolar E-Ink (E0213A367 2.13" on SPI1).
Shared by both BaseUI (heltec-mesh-solar-eink) and InkHUD (heltec-mesh-solar-inkhud).

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/E0213A367.h"

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

class HeltecMeshSolarPanel : public NicheGraphics::Panels::E0213A367
{
  protected:
    void prePowerOn() override
    {
#ifdef PIN_SCREEN_VDD_CTL
        pinMode(PIN_SCREEN_VDD_CTL, OUTPUT);
        digitalWrite(PIN_SCREEN_VDD_CTL, HIGH);
        delay(10);
#endif
    }
};

#ifdef MESHTASTIC_INCLUDE_INKHUD
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    auto *panel = new HeltecMeshSolarPanel();
    Drivers::EInk *driver = panel->create();

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(10, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.rotation = 3;
    inkhud->persistence->settings.userTiles.count = 1;
    inkhud->persistence->settings.optionalMenuItems.nextTile = true;

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
    buttons->start();
}
#else
void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new HeltecMeshSolarPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(10, 1.5f);
    return display;
}
#endif

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
