#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./UC8175.h"

namespace NicheGraphics::Drivers
{

// UC8253 uses the UC81xx image commands and an active-low BUSY signal.
class GDEQ031T10 : public UC8175
{
  public:
    GDEQ031T10() : UC8175(240, 320, static_cast<UpdateTypes>(FULL | FAST)) {}
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override;

  protected:
    void reset() override;
    void configCommon() override;
    void configFull() override;
    void configFast() override;
    void writeImage(uint8_t command, const uint8_t *image) override;
    void writeNewImage() override;
    void finalizeUpdate() override;

  private:
    void setWindow();
};

} // namespace NicheGraphics::Drivers

#endif
