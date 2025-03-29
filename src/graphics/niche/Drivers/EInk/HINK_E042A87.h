/*

E-Ink display driver
    - HINK-E042A87
    - Manufacturer: Holitech
    - Size: 4.2 inch
    - Resolution: 400px x 300px
    - Flex connector marking: HINK-E042A07-FPC-A1
    - Silver sticker with QR code, marked: HE042A87

    Note: as of Feb. 2025, these panels are used for "WeActStudio 4.2in B&W" display modules

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class HINK_E042A87 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 400;
    static constexpr uint32_t height = 300;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    HINK_E042A87() : SSD16XX(width, height, supported) {}

  protected:
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS