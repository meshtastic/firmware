/*

NicheGraphics setup for LILYGO T-Deck Pro (GDEQ031T10 3.1" 240x320 on the default SPI bus).

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEQ031T10.h"

class TDeckProPanel : public NicheGraphics::Panels::GDEQ031T10
{
  protected:
    SPIClass *beginSpi() override
    {
        // Old GxEPD2 path used the global SPI bus on this board.
        SPI.begin();
        return &SPI;
    }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new TDeckProPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    // T-Deck Pro types frequently; allow more FAST refreshes between FULLs than the default.
    display->setDisplayResilience(15, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
