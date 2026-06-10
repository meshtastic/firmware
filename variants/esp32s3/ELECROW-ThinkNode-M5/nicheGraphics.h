/*

NicheGraphics setup for Elecrow ThinkNode-M5 (GDEY0154D67 1.54" on HSPI).

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEY0154D67.h"

class ThinkNodeM5Panel : public NicheGraphics::Panels::GDEY0154D67
{
  protected:
    SPIClass *beginSpi() override
    {
        auto *hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
        return hspi;
    }
    // Old GxEPD2 path used setRotation(4) (= 0° + mirror). BaseUIEInkDisplay does not implement
    // mirroring; expose 0 here and rely on hardware orientation. Adjust if testing shows otherwise.
  public:
    uint8_t rotation() const override { return 0; }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new ThinkNodeM5Panel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(10, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
