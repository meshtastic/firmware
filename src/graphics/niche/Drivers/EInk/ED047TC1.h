/*

    E-Ink display driver adapter
        - ED047TC1 (via FastEPD library)
        - Manufacturer: E Ink / used in LilyGo T5-E-Paper-S3-Pro
        - Size: 4.7 inch
        - Resolution: 960px x 540px
        - Interface: 8-bit parallel (NOT SPI)

    Unlike the other NicheGraphics EInk drivers, this one drives a parallel e-paper
    panel via the FastEPD library. SPI parameters passed to begin() are ignored.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

// Forward declare to avoid pulling FastEPD into all translation units
class FASTEPD;

namespace NicheGraphics::Drivers
{

class ED047TC1 : public EInk
{
    static constexpr uint16_t DISPLAY_WIDTH = 960;
    static constexpr uint16_t DISPLAY_HEIGHT = 540;
    static constexpr UpdateTypes supported = static_cast<UpdateTypes>(FULL | FAST);

  public:
    ED047TC1() : EInk(DISPLAY_WIDTH, DISPLAY_HEIGHT, supported) {}

    // EInk interface — SPI params are not used for this parallel display
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = 0xFF) override;
    void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    bool isUpdateDone() override { return true; } // FastEPD updates are blocking

  private:
    FASTEPD *epaper = nullptr;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
