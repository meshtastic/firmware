/*

NicheGraphics setup for Heltec Wireless Paper (HSPI; runtime-detected 2.13" panel).

Panel choice depends on detectEInk() output:
  - LCMEN213EFC1 (V1.1)
  - E0213A367 (V1.1.1, V1.2)

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/E0213A367.h"
#include "graphics/eink/Panels/LCMEN213EFC1.h"

#include "einkDetect.h"

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

template <typename Base> class HeltecWPPanel : public Base
{
  protected:
    SPIClass *beginSpi() override
    {
        auto *hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
        return hspi;
    }
};

struct HeltecWPDetectedDriver {
    NicheGraphics::Drivers::EInk *driver;
    uint8_t fastPerFull;
    float stress;
    uint8_t rotation;
};

static HeltecWPDetectedDriver detectAndCreateHeltecWPDriver()
{
    EInkDetectionResult model = detectEInk();
    HeltecWPDetectedDriver out{nullptr, 10, 1.5f, 3};
    if (model == EInkDetectionResult::LCMEN213EFC1) {
        auto *p = new HeltecWPPanel<NicheGraphics::Panels::LCMEN213EFC1>();
        out.driver = p->create();
        out.rotation = p->rotation();
        out.fastPerFull = 10;
        out.stress = 1.5f;
    } else {
        auto *p = new HeltecWPPanel<NicheGraphics::Panels::E0213A367>();
        out.driver = p->create();
        out.rotation = p->rotation();
        out.fastPerFull = 15;
        out.stress = 3.0f;
    }
    return out;
}

#ifdef MESHTASTIC_INCLUDE_INKHUD
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    HeltecWPDetectedDriver detected = detectAndCreateHeltecWPDriver();

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(detected.driver);
    inkhud->setDisplayResilience(detected.fastPerFull, detected.stress);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.rotation = 3;
    inkhud->persistence->settings.userTiles.count = 1;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false);

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
    HeltecWPDetectedDriver detected = detectAndCreateHeltecWPDriver();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(detected.driver, detected.rotation);
    display->setDisplayResilience(detected.fastPerFull, detected.stress);
    return display;
}
#endif

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
