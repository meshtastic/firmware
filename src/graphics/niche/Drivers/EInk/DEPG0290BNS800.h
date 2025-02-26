/*

E-Ink display driver
    - DEPG0290BNS800
    - Manufacturer: DKE
    - Size: 2.9 inch
    - Resolution: 128px x 296px
    - Flex connector marking: FPC-7519 rev.b

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class DEPG0290BNS800 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 128;
    static constexpr uint32_t height = 296;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    DEPG0290BNS800() : SSD16XX(width, height, supported, 1) {} // Note: left edge of this display is offset by 1 byte

  protected:
    void configVoltages() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
    void finalizeUpdate() override; // Only overriden for a slight optimization
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS