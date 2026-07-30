/*

NicheGraphics setup for MeshLink (LoraItalia) ePaper build (GDEY0213B74 2.13" on SPI1).
Only used by env:meshlink_eink (the env:meshlink build has no display).

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"
#include "graphics/eink/Panels/GDEY0213B74.h"

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new NicheGraphics::Panels::GDEY0213B74();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(5, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
