/*

NicheGraphics setup for the CrowPanel ESP32-S3 ePaper boards.

This directory is shared by three envs that each pick a different panel via
CROWPANEL_ESP32S3_{2,4,5}_EPAPER macros in platformio.ini.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/BaseUIEInkDisplay.h"

#if defined(CROWPANEL_ESP32S3_2_EPAPER)
#include "graphics/eink/Panels/GDEY029T94.h"
using CrowpanelBasePanel = NicheGraphics::Panels::GDEY029T94;
#elif defined(CROWPANEL_ESP32S3_4_EPAPER)
#include "graphics/eink/Panels/HINK_E042A87.h"
using CrowpanelBasePanel = NicheGraphics::Panels::HINK_E042A87;
#elif defined(CROWPANEL_ESP32S3_5_EPAPER)
#include "graphics/eink/Panels/GDEY0579T93.h"
using CrowpanelBasePanel = NicheGraphics::Panels::GDEY0579T93;
#else
#error "No CROWPANEL_ESP32S3_*_EPAPER macro defined for this env"
#endif

class CrowpanelPanel : public CrowpanelBasePanel
{
  protected:
    SPIClass *beginSpi() override
    {
        auto *hspi = new SPIClass(HSPI);
        hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
        return hspi;
    }
    void prePowerOn() override
    {
#ifdef VEXT_ENABLE
        pinMode(VEXT_ENABLE, OUTPUT);
        digitalWrite(VEXT_ENABLE, VEXT_ON_VALUE);
        delay(10);
#endif
    }
};

void setupNicheGraphics() {}

NicheGraphics::BaseUIEInkDisplay *setupNicheGraphicsBaseUI()
{
    auto *panel = new CrowpanelPanel();
    NicheGraphics::Drivers::EInk *driver = panel->create();
    auto *display = new NicheGraphics::BaseUIEInkDisplay(driver, panel->rotation());
    display->setDisplayResilience(20, 1.5f);
    return display;
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
