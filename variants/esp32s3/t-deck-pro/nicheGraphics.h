#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/Drivers/EInk/GDEQ031T10.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/Inputs/TwoButton.h"
// Applets
#include "graphics/niche/InkHUD/Applets/Bases/Map/SDMapTiles.h"
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Waypoints/WaypointListApplet.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // Display, radio and SD share the default SPI bus and spiLock.
    auto *driver = new Drivers::GDEQ031T10;
    driver->begin(&SPI, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, tDeckProIsV11() ? 16 : PIN_EINK_RES);
    InkHUD::SDMapTiles::preload();

    auto *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    inkhud->setDisplayResilience(7, 1.5);
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1253;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1253;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1253;
    auto &settings = inkhud->persistence->settings;
    settings.userTiles.maxCount = 2;
    settings.userTiles.count = 1;
    settings.rotation = 0;
    settings.optionalFeatures.batteryIcon = true;
    settings.optionalMenuItems.backlight = false;
    if (tDeckProIsV11())
        Drivers::LatchingBacklight::getInstance()->setPin(45);
    settings.joystick.enabled = false;
    inkhud->setTouchEnabledProvider([]() { return true; });

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, false,
                      false); // Not Active, not autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true,
                      true); // Activated, Autoshown
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true,
                      true); // Activated, Autoshown
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1), false,
                      false); // Not Active, not autoshown
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true,
                      false); // Activated, not autoshown
    inkhud->addApplet("Waypoints", new InkHUD::WaypointListApplet, false,
                      false); // Not Active, not autoshown
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet, true,
                      false); // Activated, not autoshown
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false,
                      0); // Activated, not autoshown, default on tile 0
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, false,
                      false); // Not Active, not autoshown

    inkhud->begin();
    settings.optionalMenuItems.backlight = false;
    settings.joystick.enabled = false;
    settings.joystick.alignment = (4 - settings.rotation) % 4;

    auto *buttons = Inputs::TwoButton::getInstance();
    buttons->setWiring(0, BUTTON_PIN);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });
    buttons->start();
}

#endif
