/*

NicheGraphics setup for the MakerPython nRF52840 + SX1280 DIY e-ink board
(as shipped: GDEW029T5D, 128x296, UC8151D controller).

The UC8151D init/command set is close enough to SSD1680 that GDEY029T94
(same 128x296 geometry, SSD1680-based) is used as the panel profile.
If the panel misbehaves, drop in a UC8151D profile here.

Uses SPI1 per variant.h (SCLK=P0.02, MOSI=P0.28), which is the default
for nRF52 panel profiles.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEY029T94.h"

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new NicheGraphics::Panels::GDEY029T94();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(7, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
