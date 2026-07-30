/*

NicheGraphics setup for Heltec Wireless Paper V1.0 (DEPG0213BNS800 on HSPI).

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/DEPG0213BNS800.h"

class HeltecWirelessPaperV1Panel : public NicheGraphics::Panels::DEPG0213BNS800
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
    auto *panel = new HeltecWirelessPaperV1Panel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(5, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
