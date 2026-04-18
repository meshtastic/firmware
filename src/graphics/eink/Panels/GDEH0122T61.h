#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/GDEH0122T61.h"

namespace NicheGraphics::Panels
{
class GDEH0122T61 : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        prePowerOn();
        SPIClass *spi = beginSpi();
        auto *drv = new NicheGraphics::Drivers::GDEH0122T61();
        drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
        return drv;
    }
};
} // namespace NicheGraphics::Panels

#endif
