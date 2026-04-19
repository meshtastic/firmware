/*

NicheGraphics setup for LILYGO T5 ePaper Pro (ED047TC1, 960x540, parallel via FastEPD).

Three envs share this directory:
  - t5s3-epaper-v1     (BaseUI, BB_PANEL_LILYGO_T5PRO)
  - t5s3-epaper-v2     (BaseUI, BB_PANEL_LILYGO_T5PRO_V2 + GPIO-expander setup)
  - t5s3_epaper_inkhud (InkHUD on V2)

The active panel is selected by the T5_S3_EPAPER_PRO_V1 / T5_S3_EPAPER_PRO_V2 macros from platformio.ini.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "FastEPD.h"

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Drivers/EInkParallel.h"
#include "graphics/eink/Panels/T5Epaper.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "graphics/niche/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/Applets/User/DM/DMApplet.h"
#include "graphics/niche/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD.h"
#endif

#if defined(T5_S3_EPAPER_PRO_V2)
class T5V2Driver : public NicheGraphics::Drivers::EInkParallel
{
  public:
    T5V2Driver() : EInkParallel(960, 540, BB_PANEL_LILYGO_T5PRO_V2, 28000000) {}

  protected:
    void postPanelInit() override
    {
        // V2 uses a GPIO expander; raise port-0 pins 0..7 high (as the legacy driver did).
        FASTEPD *epd = fastEpd();
        for (int i = 0; i < 8; i++) {
            epd->ioPinMode(i, OUTPUT);
            epd->ioWrite(i, HIGH);
        }
    }
};
#elif defined(T5_S3_EPAPER_PRO_V1)
class T5V1Driver : public NicheGraphics::Drivers::EInkParallel
{
  public:
    T5V1Driver() : EInkParallel(960, 540, BB_PANEL_LILYGO_T5PRO, 28000000) {}
};
#else
#error "t5s3_epaper requires T5_S3_EPAPER_PRO_V1 or T5_S3_EPAPER_PRO_V2"
#endif

class T5EpaperPanel : public NicheGraphics::Panels::T5EpaperPanel
{
  protected:
    NicheGraphics::Drivers::EInkParallel *makeDriver() override
    {
#if defined(T5_S3_EPAPER_PRO_V2)
        return new T5V2Driver();
#else
        return new T5V1Driver();
#endif
    }
};

#ifdef MESHTASTIC_INCLUDE_INKHUD
void setupNicheGraphics()
{
    using namespace NicheGraphics;

    auto *panel = new T5EpaperPanel();
    Drivers::EInk *driver = panel->create();

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(20, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.userTiles.maxCount = 4;
    inkhud->persistence->settings.userTiles.count = 1;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);

    inkhud->begin();
}
#else
void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new T5EpaperPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, 0);
    display->setDisplayResilience(20, 1.5f);
    return display;
}
#endif

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
