// InkHUD setup for the RockBase IoT NM-EPD-420.
//
// Display: 4.2" 400×300 tri-color panel. The board-specific driver detects SSD1683
// or UC8179 and keeps InkHUD monochrome; SSD1683 can use differential B/W refresh.
//
// LoRa and EPD share no SPI bus (EPD = FSPI/default, LoRa = HSPI), so InkHUD's EPD
// transactions and Router TX do not contend for the SPI mutex.

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/niche/InkHUD/InkHUD.h"

#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

#include "graphics/niche/Drivers/EInk/NMEPD420.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "buzz.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // EPD lives on the FSPI pins on this board but LoRa already owns the default `SPI`
    // (FSPI) global; allocate a fresh HSPI peripheral and route it to the EPD GPIOs via
    // the ESP32-S3 GPIO matrix so the two never contend.
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    Drivers::EInk *driver = new Drivers::NMEPD420;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);

    // 4.2" panel can tolerate a generous fast-refresh budget before forcing a full
    // refresh; the larger area means a full refresh is the long pole on this board.
    inkhud->setDisplayResilience(7, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Two-tile default layout — 400×300 is plenty for side-by-side panes.
    inkhud->persistence->settings.userTiles.maxCount = 4;
    inkhud->persistence->settings.userTiles.count = 2;
    inkhud->persistence->settings.rotation = 0;
    inkhud->persistence->settings.optionalMenuItems.nextTile = true;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true, 0);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true, false, 1);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 1);

    inkhud->begin();

    // Buttons: BOOT (GPIO0) drives InkHUD short/long press; USER (GPIO45) cycles tiles.
    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance();

    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    buttons->setWiring(1, PIN_BUTTON2);
    buttons->setHandlerShortPress(1, [inkhud]() {
        inkhud->nextTile();
        playChirp();
    });

    buttons->start();
}

#endif
