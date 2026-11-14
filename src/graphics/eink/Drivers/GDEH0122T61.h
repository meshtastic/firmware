/*

E-Ink display driver
    - GDEH0122T61
    - Manufacturer: Good Display
    - Size: 1.22 inch
    - Resolution: 192px x 176px
    - Controller IC: SSD1681 (operating in a sub-200x200 window)

    Used by: t-echo-lite.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class GDEH0122T61 : public SSD16XX
{
  private:
    static constexpr uint32_t width = 192;
    static constexpr uint32_t height = 176;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEH0122T61() : SSD16XX(width, height, supported) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
