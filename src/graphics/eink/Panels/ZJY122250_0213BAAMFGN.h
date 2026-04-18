#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/ZJY122250_0213BAAMFGN.h"

namespace NicheGraphics::Panels
{
class ZJY122250_0213BAAMFGN : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        prePowerOn();
        SPIClass *spi = beginSpi();
        auto *drv = new NicheGraphics::Drivers::ZJY122250_0213BAAMFGN();
        drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
        return drv;
    }
    uint8_t rotation() const override { return 1; }
};
} // namespace NicheGraphics::Panels

#endif
