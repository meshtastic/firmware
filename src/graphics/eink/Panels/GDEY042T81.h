#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/GDEY042T81.h"

namespace NicheGraphics::Panels
{
class GDEY042T81 : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        prePowerOn();
        SPIClass *spi = beginSpi();
        auto *drv = new NicheGraphics::Drivers::GDEY042T81();
        drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
        return drv;
    }
};
} // namespace NicheGraphics::Panels

#endif
