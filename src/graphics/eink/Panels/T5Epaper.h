/*

Panel profile base for the LILYGO T5 ePaper Pro family (ED047TC1, 960x540, 8-bit parallel via FastEPD).

V1 and V2 use different FastEPD panel IDs and V2 also needs GPIO-expander pins raised.
Variants subclass to provide a Drivers::EInkParallel subclass that implements
postPanelInit() if needed.

*/

#pragma once

#include "configuration.h"

#if defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) && defined(ARCH_ESP32) && defined(NICHE_HAS_FASTEPD)

#include "./PanelProfile.h"
#include "graphics/eink/Drivers/EInkParallel.h"

namespace NicheGraphics::Panels
{
class T5EpaperPanel : public PanelProfile
{
  public:
    NicheGraphics::Drivers::EInk *create() override
    {
        auto *drv = makeDriver();
        drv->begin(nullptr, 0, 0, 0); // SPI args ignored
        return drv;
    }

  protected:
    // Variant returns a Drivers::EInkParallel subclass configured for its specific panel/init.
    virtual NicheGraphics::Drivers::EInkParallel *makeDriver() = 0;
};
} // namespace NicheGraphics::Panels

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS && ARCH_ESP32 && NICHE_HAS_FASTEPD
