/*

NicheGraphics setup for RAK4631 + RAK14000 ePaper module (2.13" BN, DEPG0213BNS800 on SPI1).
BaseUI build only - no InkHUD env.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/DEPG0213BNS800.h"

class RAK4631EpaperPanel : public NicheGraphics::Panels::DEPG0213BNS800
{
  protected:
    SPIClass *beginSpi() override
    {
        // RAK14000 is wired to SPI1 via pins declared in variant.h.
        // Init settings cloned from the legacy GxEPD2 path on this board.
        SPI1.begin();
        return &SPI1;
    }
};

void setupNicheGraphics()
{
    // BaseUI handles buttons through its default ButtonThread. Nothing extra here.
}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new RAK4631EpaperPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(7, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
