/*

NicheGraphics setup for Heltec Vision Master E-290.

Supports two UI builds that share this file:
  - InkHUD build  (MESHTASTIC_INCLUDE_INKHUD defined)
  - BaseUI build  (MESHTASTIC_INCLUDE_INKHUD not defined)

The panel itself (DEPG0290BNS800, SSD1680, 128x296) is defined once in
  graphics/eink/Panels/DEPG0290BNS800.h
The variant only overrides what is specific to this board — here, that's the HSPI pin bring-up.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/DEPG0290BNS800.h"

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

// Board-specific: display is wired to HSPI with explicit pins.
class VMe290Panel : public NicheGraphics::Panels::DEPG0290BNS800
{
  protected:
    SPIClass *beginSpi() override
    {
        auto *hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
        return hspi;
    }
};

static NicheGraphics::Drivers::EInk *createVMe290Driver(uint8_t *outRotation)
{
    auto *panel = new VMe290Panel();
    *outRotation = panel->rotation();
    return panel->create();
}

#ifdef MESHTASTIC_INCLUDE_INKHUD
static void setupVMe290Buttons(std::function<void()> shortPress, std::function<void()> longPress,
                               std::function<void()> auxShortPress)
{
    using namespace NicheGraphics;
    auto *buttons = Inputs::TwoButton::getInstance();

    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, shortPress);
    buttons->setHandlerLongPress(0, longPress);

    buttons->setWiring(1, PIN_BUTTON2);
    buttons->setHandlerShortPress(1, auxShortPress);

    buttons->start();
}

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    uint8_t panelRotation;
    Drivers::EInk *driver = createVMe290Driver(&panelRotation);
    (void)panelRotation; // InkHUD uses its own settings.rotation below

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(7, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.rotation = 1;
    inkhud->persistence->settings.userTiles.count = 1;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);

    inkhud->begin();

    setupVMe290Buttons([inkhud]() { inkhud->shortpress(); }, [inkhud]() { inkhud->longpress(); },
                       [inkhud]() {
                           inkhud->nextTile();
                           playChirp();
                       });
}
#else  // BaseUI path
void setupNicheGraphics()
{
    // BaseUI handles its own input via the default ButtonThread. Nothing extra to set up here.
}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    uint8_t panelRotation;
    NicheGraphics::Drivers::EInk *driver = createVMe290Driver(&panelRotation);
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panelRotation);
    display->setDisplayResilience(7, 1.5f);
    return display;
}
#endif // MESHTASTIC_INCLUDE_INKHUD

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
