/*

NicheGraphics setup for LILYGO Mini ePaper S3 (GDEW0102T4 1.02" on HSPI).
Shared by both BaseUI and InkHUD envs.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Drivers/GDEW0102T4.h"
#include "graphics/eink/Panels/GDEW0102T4.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "graphics/niche/Applet.h"
#include "graphics/niche/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/Applets/User/DM/DMApplet.h"
#include "graphics/niche/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD.h"
#include "graphics/niche/Inputs/TwoButtonExtended.h"
#include "graphics/niche/SystemApplet.h"
#endif

class MiniEpaperS3Panel : public NicheGraphics::Panels::GDEW0102T4
{
  public:
    uint8_t rotation() const override { return 0; }

  protected:
    void prePowerOn() override
    {
        pinMode(PIN_EINK_EN, OUTPUT);
        digitalWrite(PIN_EINK_EN, HIGH);
        delay(10);
    }
    SPIClass *beginSpi() override
    {
        auto *hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
        return hspi;
    }
};

static NicheGraphics::Drivers::EInk *makeMiniEpaperS3Driver()
{
    auto *panel = new MiniEpaperS3Panel();
    auto *driver = panel->create();
    // Tuned fast-refresh values: reg30 reg50 reg82 lutW2 lutB2 = 11 F2 04 11 0D
    static_cast<NicheGraphics::Drivers::GDEW0102T4 *>(driver)->setFastConfig({0x11, 0xF2, 0x04, 0x11, 0x0D});
    return driver;
}

#ifdef MESHTASTIC_INCLUDE_INKHUD
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    Drivers::EInk *driver = makeMiniEpaperS3Driver();

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(5, 1.5);
    inkhud->twoWayRocker = true;

    InkHUD::Applet::fontLarge = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_6PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.rotation = 0;
    inkhud->persistence->settings.userTiles.maxCount = 1;
    inkhud->persistence->settings.userTiles.count = 1;
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.aligned = true;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, false, false);
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, false);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, true);
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1), false, false);
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet, false, false);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);
    inkhud->begin();

    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.aligned = true;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    Inputs::TwoButtonExtended *buttons = Inputs::TwoButtonExtended::getInstance();
    buttons->setWiring(0, INPUTDRIVER_TWO_WAY_ROCKER_BTN, true);
    buttons->setTiming(0, 75, 300);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    buttons->setTwoWayRockerWiring(INPUTDRIVER_TWO_WAY_ROCKER_LEFT, INPUTDRIVER_TWO_WAY_ROCKER_RIGHT, true);
    buttons->setJoystickDebounce(50);
    buttons->setTwoWayRockerPressHandlers(
        [inkhud]() {
            bool systemHandlingInput = false;
            for (const InkHUD::SystemApplet *sa : inkhud->systemApplets) {
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
            for (const InkHUD::SystemApplet *sa : inkhud->systemApplets) {
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
#else
void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    NicheGraphics::Drivers::EInk *driver = makeMiniEpaperS3Driver();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, 0);
    display->setDisplayResilience(3, 1.5f);
    return display;
}
#endif

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
