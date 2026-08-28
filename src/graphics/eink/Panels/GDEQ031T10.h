#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/GDEQ031T10.h"

namespace NicheGraphics::Panels
{
class GDEQ031T10 : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        prePowerOn();
        SPIClass *spi = beginSpi();
        auto *drv = new NicheGraphics::Drivers::GDEQ031T10();
        drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
        return drv;
    }
};
} // namespace NicheGraphics::Panels

#endif
