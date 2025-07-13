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
#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/Drivers/EInk/GDEY0154D67.h"
#include "graphics/niche/Inputs/TwoButton.h"

// Special case - fix T-Echo's touch button
// ----------------------------------------
// On a handful of T-Echos, LoRa TX triggers the capacitive touch
// To avoid this, we lockout the button during TX
#include "mesh/RadioLibInterface.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // For NRF52 platforms, SPI pins are defined in variant.h
    SPI1.begin();

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver = new Drivers::GDEY0154D67;
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(20, 1.5);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2;              // Two applets side-by-side
    inkhud->persistence->settings.rotation = 3;                        // 270 degrees clockwise
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true; // Device definitely has a battery
    inkhud->persistence->settings.optionalMenuItems.backlight = true;  // Until proves capacitive button works by touching it

    // Setup backlight controller
    // Note: AUX button attached further down
    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(PIN_EINK_EN);

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, no autoshow, default on tile 0

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // #0: Main User Button
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setTiming(0, 75, 500);
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // #1: Aux Button (Capacitive Touch Button)
    // - short: momentary backlight
    // - long: latch backlight on
    buttons->setWiring(1, PIN_BUTTON_TOUCH);
    buttons->setTiming(1, 50, 5000); // 5 seconds before latch - limited by T-Echo's capacitive touch IC

    buttons->setHandlerDown(1, [inkhud, backlight]() {
        // Discard the button press if radio is active
        // Rare hardware fault: LoRa activity triggers touch button
        if (!RadioLibInterface::instance || RadioLibInterface::instance->isSending())
            return;

        // Backlight on (while held)
        backlight->peek();

        // Handler has run, which confirms touch button wasn't removed as part of DIY build.
        // No longer need the fallback backlight toggle in menu.
        inkhud->persistence->settings.optionalMenuItems.backlight = false;
    });

    buttons->setHandlerLongPress(1, [backlight]() { backlight->latch(); });
    buttons->setHandlerShortPress(1, [backlight]() { backlight->off(); });

    // Begin handling button events
    buttons->start();
}

#endif