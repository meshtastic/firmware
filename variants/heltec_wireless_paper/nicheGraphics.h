#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/LCMEN2R13EFC1.h"
#include "graphics/niche/Drivers/EInk/E0213A367.h"
#include "graphics/niche/Inputs/TwoButton.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    pinMode(PIN_EINK_SCLK, OUTPUT); 
    pinMode(PIN_EINK_DC, OUTPUT); 
    pinMode(PIN_EINK_CS, OUTPUT);
    pinMode(PIN_EINK_RES, OUTPUT);
    
    //rest e-ink
    digitalWrite(PIN_EINK_RES, LOW);
    delay(20);
    digitalWrite(PIN_EINK_RES, HIGH);
    delay(20);

    digitalWrite(PIN_EINK_DC, LOW);
    digitalWrite(PIN_EINK_CS, LOW);

    // write cmd
    uint8_t cmd = 0x2F;
    pinMode(PIN_EINK_MOSI, OUTPUT);  
    digitalWrite(PIN_EINK_SCLK, LOW);
    for (int i = 0; i < 8; i++)
    {
        digitalWrite(PIN_EINK_MOSI, (cmd & 0x80) ? HIGH : LOW);
        cmd <<= 1;
        digitalWrite(PIN_EINK_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(PIN_EINK_SCLK, LOW);
        delayMicroseconds(1);
    }
    delay(10);

    digitalWrite(PIN_EINK_DC, HIGH);
    pinMode(PIN_EINK_MOSI, INPUT_PULLUP); 

    // read chip ID
    uint8_t chipId = 0;
    for (int8_t b = 7; b >= 0; b--) 
    {
      digitalWrite(PIN_EINK_SCLK, LOW);  
      delayMicroseconds(1);
      digitalWrite(PIN_EINK_SCLK, HIGH);
      delayMicroseconds(1);
      if (digitalRead(PIN_EINK_MOSI)) chipId |= (1 << b);  
    }
    digitalWrite(PIN_EINK_CS, HIGH);
    LOG_INFO("eink chipId: %02X", chipId);

    // SPI
    // -----------------------------

    // Display is connected to HSPI
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    // E-Ink Driver
    // -----------------------------
    Drivers::EInk *driver;
    if((chipId &0x03) !=0x01)
    {
       driver = new Drivers::LCMEN213EFC1;
    }
    else
    {
        driver = new Drivers::E0213A367;
    }
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(10, 1.5);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, not autoshown, default on tile 0

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // #0: Main User Button
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // No aux button on this board

    // Begin handling button events
    buttons->start();
}

#endif