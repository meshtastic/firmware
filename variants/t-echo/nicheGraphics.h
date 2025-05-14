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

// #include "graphics/niche/InkHUD/Applets/Examples/BasicExample/BasicExampleApplet.h"
// #include "graphics/niche/InkHUD/Applets/Examples/NewMsgExample/NewMsgExampleApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/Drivers/EInk/GDEY0154D67.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "graphics/niche/Fonts/FreeSans6pt7b.h"
#include "graphics/niche/Fonts/FreeSans6pt8bCyrillic.h"
#include <Fonts/FreeSans9pt7b.h>

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

    // For NRF52 platforms, SPI pins are defined in variant.h, not passed to begin()
    SPIClass *inkSPI = &SPI1;
    inkSPI->begin();

    // Driver
    // -----------------------------

    // Use E-Ink driver
    Drivers::EInk *driver = new Drivers::GDEY0154D67;
    driver->begin(inkSPI, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(20, 1.5);

    // Prepare fonts
    InkHUD::Applet::fontLarge = InkHUD::AppletFont(FreeSans9pt7b);
    InkHUD::Applet::fontSmall = InkHUD::AppletFont(FreeSans6pt7b);
    /*
    // Font localization demo: Cyrillic
    InkHUD::Applet::fontSmall = InkHUD::AppletFont(FreeSans6pt8bCyrillic);
    InkHUD::Applet::fontSmall.addSubstitutionsWin1251();
    */

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2;              // Two applets side-by-side
    inkhud->persistence->settings.rotation = 3;                        // 270 degrees clockwise
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true; // Device definitely has a battery
    inkhud->persistence->settings.optionalMenuItems.backlight = true;  // Until proves capacitive button works by touching it

    // Setup backlight
    // Note: AUX button behavior configured further down
    Drivers::LatchingBacklight *backlight = Drivers::LatchingBacklight::getInstance();
    backlight->setPin(PIN_EINK_EN);

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true); // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0); // Activated, no autoshow, default on tile 0
    // inkhud->addApplet("Basic", new InkHUD::BasicExampleApplet);
    // inkhud->addApplet("NewMsg", new InkHUD::NewMsgExampleApplet);

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // (To improve code readability only)
    constexpr uint8_t MAIN_BUTTON = 0;
    constexpr uint8_t TOUCH_BUTTON = 1;

    // Setup the main user button
    buttons->setWiring(MAIN_BUTTON, Inputs::TwoButton::getUserButtonPin());
    buttons->setTiming(MAIN_BUTTON, 75, 500);
    buttons->setHandlerShortPress(MAIN_BUTTON, []() { InkHUD::InkHUD::getInstance()->shortpress(); });
    buttons->setHandlerLongPress(MAIN_BUTTON, []() { InkHUD::InkHUD::getInstance()->longpress(); });

    // Setup the capacitive touch button
    // - short: momentary backlight
    // - long: latch backlight on
    buttons->setWiring(TOUCH_BUTTON, PIN_BUTTON_TOUCH);
    buttons->setTiming(TOUCH_BUTTON, 50, 5000); // 5 seconds before latch - limited by T-Echo's capacitive touch IC
    buttons->setHandlerDown(TOUCH_BUTTON, [backlight]() {
        // Discard the button press if radio is active
        // Rare hardware fault: LoRa activity triggers touch button
        if (!RadioLibInterface::instance || RadioLibInterface::instance->isSending())
            return;

        // Backlight on (while held)
        backlight->peek();

        // Handler has run, which confirms touch button wasn't removed as part of DIY build.
        // No longer need the fallback backlight toggle in menu.
        InkHUD::InkHUD::getInstance()->persistence->settings.optionalMenuItems.backlight = false;
    });
    buttons->setHandlerLongPress(TOUCH_BUTTON, [backlight]() { backlight->latch(); });
    buttons->setHandlerShortPress(TOUCH_BUTTON, [backlight]() { backlight->off(); });

    // Begin handling button events
    buttons->start();
}

#endif