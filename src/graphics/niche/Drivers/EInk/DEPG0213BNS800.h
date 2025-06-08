/*

E-Ink display driver
    - DEPG0213BNS800
    - Manufacturer: DKE
    - Size: 2.13 inch
    - Resolution: 122px x 250px
    - Flex connector marking (not a unique identifier): FPC-7528B

    Note: this is from an older generation of DKE panels, which still used Solomon Systech controller ICs.
    DKE's website suggests that the latest DEPG0213BN displays may use Fitipower controllers instead.
*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class DEPG0213BNS800 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    DEPG0213BNS800() : SSD16XX(width, height, supported, 1) {} // Note: left edge of this display is offset by 1 byte

  protected:
    void configVoltages() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
    void finalizeUpdate() override; // Only overriden for a slight optimization
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS