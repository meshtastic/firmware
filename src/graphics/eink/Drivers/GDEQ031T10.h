/*

E-Ink display driver
    - GDEQ031T10
    - Manufacturer: Good Display
    - Size: 3.1 inch
    - Resolution: 240px x 320px
    - Controller IC: SSD1677 (SSD16XX-family, larger memory range)

    Used by: t-deck-pro.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class GDEQ031T10 : public SSD16XX
{
  private:
    static constexpr uint32_t width = 240;
    static constexpr uint32_t height = 320;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEQ031T10() : SSD16XX(width, height, supported) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
