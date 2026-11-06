// =============================================================================
// config/nicheGraphics.h — InkHUD E-Ink UI Setup
// =============================================================================
// Applet-based e-ink UI framework.  Configured here per board.
//
// Input: SixKeyNavInput turns the six keys into InputBroker events; the bridge
// below maps the nav events onto InkHUD.  Fn combinations and shutdown are
// handled by NavCombos.
//   Up / Down / Left / Right → inkhud->navUp/Down/Left/Right()
//   Enter                    → inkhud->shortpress()
//   Fn + nav                 → Fn combo callbacks (BLE/GPS/LoRa toggle, etc.)
//   Fn held ≥ 4 s            → shutdown
// =============================================================================

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/niche/InkHUD/InkHUD.h"
#include "input/NavCombos.h"

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
#include "input/InputBroker.h"

// InkHUD does not consume InputBroker events itself: upstream boards wire their
// own InkHUD input driver in this file, so bridge this board's keys over.
class InkHudNavBridge
{
  public:
    int onInputEvent(const InputEvent *event)
    {
        if (!event || event->inputEvent == INPUT_BROKER_NONE)
            return 0;

#ifdef EINK_RA01S_GP02_INKHUD
        // Any key wakes the panel; the waking press is consumed.
        if (inkHudIsDisplayOff()) {
            inkHudWakeup();
            return 0;
        }
#endif

        auto *hud = NicheGraphics::InkHUD::InkHUD::getInstance();
        switch (event->inputEvent) {
        case INPUT_BROKER_UP:
            hud->navUp();
            break;
        case INPUT_BROKER_DOWN:
            hud->navDown();
            break;
        case INPUT_BROKER_LEFT:
            hud->navLeft();
            break;
        case INPUT_BROKER_RIGHT:
            hud->navRight();
            break;
        case INPUT_BROKER_SELECT:
            hud->shortpress();
            break;
        default:
            break;
        }
        return 0;
    }
};

static InkHudNavBridge navBridge;
static CallbackObserver<InkHudNavBridge, const InputEvent *> navBridgeObserver(&navBridge, &InkHudNavBridge::onInputEvent);
#endif

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// E-Ink driver
#include "graphics/niche/Drivers/EInk/GDEY0213B74.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // -------------------------------------------------------------------------
    // Panel power — PIN_EINK_POWER (P1.10, active-LOW: LOW = ON)
    // -------------------------------------------------------------------------
    pinMode(PIN_EINK_POWER, OUTPUT);
    digitalWrite(PIN_EINK_POWER, EINK_POWER_ACTIVE);

    // -------------------------------------------------------------------------
    // SPI1 — dedicated E-Ink bus (SCLK=P0.31, MOSI=P0.29, CS=P0.30)
    // -------------------------------------------------------------------------
    SPI1.begin();

    // -------------------------------------------------------------------------
    // E-Ink driver
    // -------------------------------------------------------------------------
    Drivers::EInk *driver = new Drivers::GDEY0213B74;
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // -------------------------------------------------------------------------
    // InkHUD
    // -------------------------------------------------------------------------
    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    setInkHudDriver(driver, &SPI1);
    inkhud->setDriver(driver);

    inkhud->setDisplayResilience(20, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.rotation = 3;
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.alignment = 1;
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;
    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;

    // -------------------------------------------------------------------------
    // Applets
    // -------------------------------------------------------------------------
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Recents", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);

    inkhud->begin();

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    if (inputBroker)
        navBridgeObserver.observe(inputBroker);
#endif
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
