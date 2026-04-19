#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/GDEY0579T93.h"

namespace NicheGraphics::Panels
{
class GDEY0579T93 : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        prePowerOn();
        SPIClass *spi = beginSpi();
        auto *drv = new NicheGraphics::Drivers::GDEY0579T93();
        drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
        return drv;
    }
};
} // namespace NicheGraphics::Panels

#endif
