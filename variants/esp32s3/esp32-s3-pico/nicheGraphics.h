/*

NicheGraphics setup for the ESP32-S3-Pico board (GDEY029T94 2.9" on the default SPI bus).

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEY029T94.h"

class Esp32S3PicoPanel : public NicheGraphics::Panels::GDEY029T94
{
  protected:
    SPIClass *beginSpi() override
    {
        // Old GxEPD2 path used the default SPI bus; preserve that to avoid SPI conflicts.
        SPI.begin();
        return &SPI;
    }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new Esp32S3PicoPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(7, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
