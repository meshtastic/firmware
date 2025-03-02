/*

E-Ink display driver
    - DEPG0154BNS800
    - Manufacturer: DKE
    - Size: 1.54 inch
    - Resolution: 152px x 152px
    - Flex connector marking: FPC7525

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class DEPG0154BNS800 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 152;
    static constexpr uint32_t height = 152;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL);

  public:
    DEPG0154BNS800() : SSD16XX(width, height, supported, 1) {} // Note: left edge of this display is offset by 1 byte
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS