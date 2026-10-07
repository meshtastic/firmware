/*

E-Ink display driver
    - GDEY042T81
    - Manufacturer: Good Display
    - Size: 4.2 inch
    - Resolution: 400px x 300px
    - Controller IC: SSD1683

    Used by: ME25LS01-4Y10TD_e-ink.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class GDEY042T81 : public SSD16XX
{
  private:
    static constexpr uint32_t width = 400;
    static constexpr uint32_t height = 300;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEY042T81() : SSD16XX(width, height, supported) {}

  protected:
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
