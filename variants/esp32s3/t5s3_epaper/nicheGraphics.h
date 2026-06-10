/*

NicheGraphics setup for LILYGO T5 ePaper Pro (ED047TC1, 960x540, parallel via FastEPD).

Three envs share this directory:
  - t5s3-epaper-v1     (BaseUI, BB_PANEL_LILYGO_T5PRO)
  - t5s3-epaper-v2     (BaseUI, BB_PANEL_LILYGO_T5PRO_V2 + GPIO-expander setup)
  - t5s3_epaper_inkhud (InkHUD on V2)

The active panel is selected by the T5_S3_EPAPER_PRO_V1 / T5_S3_EPAPER_PRO_V2 macros from platformio.ini.

The 4.7" ED047TC1 is an 8-bit parallel e-paper panel, driven through the FastEPD library
via the NicheGraphics EInkParallel driver and T5EpaperPanel profile.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "FastEPD.h"

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Drivers/EInkParallel.h"
#include "graphics/eink/Panels/T5Epaper.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "graphics/niche/InkHUD.h"

// Applets
#include "graphics/niche/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/Applets/User/DM/DMApplet.h"
#include "graphics/niche/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
#include "graphics/niche/Inputs/TwoButton.h"
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

    // E-Ink Driver
    // -----------------------------
    // The ED047TC1 is a parallel display driven via the T5EpaperPanel profile.
    // panel->create() builds the EInkParallel driver and calls begin() (SPI args ignored).
    auto *panel = new T5EpaperPanel();
    Drivers::EInk *driver = panel->create();

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(7, 1.5);

    // Prepare fonts — use larger sizes to suit the 4.7" screen at ~234 DPI
    InkHUD::Applet::fontLarge = FREESANS_24PT_WIN1253;
    InkHUD::Applet::fontMedium = FREESANS_18PT_WIN1253;
    InkHUD::Applet::fontSmall = FREESANS_12PT_WIN1253;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;
    inkhud->persistence->settings.optionalMenuItems.backlight = false;

    // Alignment must cancel rotation for visual-frame touch input: (rotation + alignment) % 4 == 0.
    inkhud->persistence->settings.joystick.alignment = (4 - inkhud->persistence->settings.rotation) % 4;

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, false, false);      // Not Active, not autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, true);                         // Activated, Autoshown
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, true);   // Activated, Autoshown
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1), false, false); // Not Active, not autoshown
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true, false);           // Activated, not autoshown
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet, true, false);      // Activated, not autoshown
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, not autoshown, default on tile 0
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false, false);   // Not Active, not autoshown

    // Enable reusable InkHUD touch status indicator for this touch-capable board.
    inkhud->setTouchEnabledProvider(isTouchInputEnabled);

    // Start running InkHUD
    inkhud->begin();
    // Arm GT911 capacitive-home callback only after InkHUD startup is complete.
    t5SetHomeCapButtonEventsEnabled(true);

    // Keep Wireless Paper single-button semantics regardless of persisted settings:
    // short press advances, long press opens menu/selects.
    inkhud->persistence->settings.joystick.enabled = false;

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // A shared NicheGraphics component

    // #0: BOOT button (primary user input for InkHUD navigation on T5-S3)
#if defined(T5_S3_EPAPER_PRO_V1)
    buttons->setWiring(0, PIN_BUTTON2);
#else
    buttons->setWiring(0, BUTTON_PIN);
#endif
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    buttons->start();
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
