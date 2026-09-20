/*

E-Ink display driver
    - HINK_E0213A289
    - Manufacturer: Holitech
    - Size: 2.13 inch
    - Resolution: 122px x 250px
    - Flex connector label (not a unique identifier): FPC-7528B

    Note: as of Feb. 2025, these panels are used for "WeActStudio 2.13in B&W" display modules

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class HINK_E0213A289 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    HINK_E0213A289() : SSD16XX(width, height, supported, 1) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS