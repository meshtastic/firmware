/*

NicheGraphics setup for the Nordic pca10059 dongle + 4.2" e-ink DIY build
(as shipped: GDEW042M01, 400x300, UC8276 controller).

No UC8276 driver exists in the NicheGraphics tree, so this uses the
HINK_E042A87 profile (same 400x300 geometry, SSD1680-based). The command
sets differ; if the panel misbehaves, drop in a UC8276 profile here.

Uses SPI1 per variant.h (SCLK=P0.09, MOSI=P0.10), which is the default
for nRF52 panel profiles.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/HINK_E042A87.h"

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new NicheGraphics::Panels::HINK_E042A87();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(10, 2.0f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
