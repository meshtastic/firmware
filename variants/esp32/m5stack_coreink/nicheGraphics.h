/*

NicheGraphics setup for M5Stack CoreInk (1.54" 200x200 GDEW0154M09).

The GDEW0154M09 uses an SSD1681-class controller, so the GDEY0154D67 panel
profile (same controller family, same resolution) drives it directly.

LoRa and the e-ink panel share the default SPI bus (SCLK=18, MOSI=23);
use the board's default SPI instance rather than spinning up a separate
HSPI peripheral on the same pins.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEY0154D67.h"

class M5CoreInkPanel : public NicheGraphics::Panels::GDEY0154D67
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
    auto *panel = new M5CoreInkPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(10, 2.0f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
