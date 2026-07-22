/*

NicheGraphics setup for Heltec Vision Master E-213 (HSPI; runtime-detected 2.13" panel).

Panel choice depends on detectEInk() output:
  - LCMEN213EFC1 (V1, unmarked)
  - E0213A367 (V1.1, V1.2)

Both panels share the same SPI wiring; the difference is the controller/waveform.

The InkHUD build still uses InkHUD's internal drivers; the BaseUI build uses the shared
graphics/eink layer. The two halves merge once InkHUD moves onto the shared layer.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#ifdef MESHTASTIC_INCLUDE_INKHUD

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/E0213A367.h"
#include "graphics/niche/Drivers/EInk/LCMEN2R13EFC1.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "buzz.h"       // Button feedback
#include "einkDetect.h" // Detect display model at runtime

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // Detect E-Ink Model
    // -------------------

    EInkDetectionResult displayModel = detectEInk();

    // SPI
    // -----------------------------

    // Display is connected to HSPI
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver;

    if (displayModel == EInkDetectionResult::LCMEN213EFC1) // V1 (unmarked)
        driver = new Drivers::LCMEN213EFC1;
    else if (displayModel == EInkDetectionResult::E0213A367) // V1.1
        driver = new Drivers::E0213A367;

    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are

    if (displayModel == EInkDetectionResult::LCMEN213EFC1) // V1 (unmarked)
        inkhud->setDisplayResilience(10, 1.5);
    else if (displayModel == EInkDetectionResult::E0213A367) // V1.1
        inkhud->setDisplayResilience(15, 3);

    // Select fonts
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalMenuItems.nextTile = false; // Behavior handled by aux button instead

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);          // -
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

    // #1: Aux Button
    buttons->setWiring(1, PIN_BUTTON2);
    buttons->setHandlerShortPress(1, [inkhud]() {
        inkhud->nextTile();
        playChirp();
    });

    // Begin handling button events
    buttons->start();
}

#else // BaseUI on the shared graphics/eink layer

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/E0213A367.h"
#include "graphics/eink/Panels/LCMEN213EFC1.h"

#include "einkDetect.h" // Detect display model at runtime

template <typename Base> class VMe213Panel : public Base
{
  protected:
    SPIClass *beginSpi() override
    {
        auto *hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
        return hspi;
    }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    NicheGraphics::Drivers::EInk *driver;
    uint8_t rotation;
    uint8_t fastPerFull;
    float stress;

    if (detectEInk() == EInkDetectionResult::LCMEN213EFC1) { // V1 (unmarked)
        auto *panel = new VMe213Panel<NicheGraphics::Panels::LCMEN213EFC1>();
        driver = panel->create();
        rotation = panel->rotation();
        fastPerFull = 10;
        stress = 1.5f;
    } else { // E0213A367 (V1.1, V1.2)
        auto *panel = new VMe213Panel<NicheGraphics::Panels::E0213A367>();
        driver = panel->create();
        rotation = panel->rotation();
        fastPerFull = 15;
        stress = 3.0f;
    }

    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, rotation);
    display->setDisplayResilience(fastPerFull, stress);
    return display;
}

#endif // MESHTASTIC_INCLUDE_INKHUD

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
