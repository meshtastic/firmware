/*

E-Ink display driver
    - GDEY029T94 (also sold as GDEY029T94-V2)
    - Manufacturer: Good Display
    - Size: 2.9 inch
    - Resolution: 128px x 296px
    - Controller IC: SSD1680

    Used by: esp32-s3-pico, crowpanel-esp32s3-2-epaper.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class GDEY029T94 : public SSD16XX
{
  private:
    static constexpr uint32_t width = 128;
    static constexpr uint32_t height = 296;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEY029T94() : SSD16XX(width, height, supported) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
