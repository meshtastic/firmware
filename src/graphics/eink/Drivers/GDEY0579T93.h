/*

E-Ink display driver
    - GDEY0579T93
    - Manufacturer: Good Display
    - Size: 5.79 inch
    - Resolution: 792px x 272px
    - Controller IC: SSD1683 (extended memory range)

    Used by: crowpanel-esp32s3-5-epaper.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class GDEY0579T93 : public SSD16XX
{
  private:
    static constexpr uint32_t width = 792;
    static constexpr uint32_t height = 272;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEY0579T93() : SSD16XX(width, height, supported) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
