/*

NicheGraphics setup for the DIY ESP32-S3 2.9" e-ink reference board
(as shipped: GDEW029T5D, 128x296, UC8151D controller).

The UC8151D init/command set is close enough to SSD1680 that GDEY029T94
(same 128x296 geometry, SSD1680-based) is used as the panel profile.
If the specific UC8151D panel behaves oddly (ghosting, inverted colours,
wrong refresh sequence), swap this panel profile for a UC8151D one.

Uses the default SPI bus, which is shared with the SX1280 LoRa module.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEY029T94.h"

class DiyEsp32S3EinkPanel : public NicheGraphics::Panels::GDEY029T94
{
  protected:
    SPIClass *beginSpi() override
    {
        SPI.begin();
        return &SPI;
    }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new DiyEsp32S3EinkPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(7, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
