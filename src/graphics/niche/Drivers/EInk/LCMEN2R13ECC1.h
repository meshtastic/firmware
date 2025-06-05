/*

E-Ink display driver
    - SSD1680
    - Manufacturer: WISEVAST
    - Size: 2.13 inch
    - Resolution: 122px x 255px
    - Flex connector marking: Soldering connector, no connector is needed

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class LCMEN2R13ECC1 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    LCMEN2R13ECC1() : SSD16XX(width, height, supported, 1) {} // Note: left edge of this display is offset by 1 byte

  protected:
    virtual void configScanning() override;
    virtual void configWaveform() override;
    virtual void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS