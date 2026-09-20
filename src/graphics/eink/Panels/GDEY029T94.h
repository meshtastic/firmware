#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/GDEY029T94.h"

namespace NicheGraphics::Panels
{
class GDEY029T94 : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        prePowerOn();
        SPIClass *spi = beginSpi();
        auto *drv = new NicheGraphics::Drivers::GDEY029T94();
        drv->begin(spi, pinDC(), pinCS(), pinBusy(), pinReset());
        return drv;
    }
    uint8_t rotation() const override { return 1; }
};
} // namespace NicheGraphics::Panels

#endif
