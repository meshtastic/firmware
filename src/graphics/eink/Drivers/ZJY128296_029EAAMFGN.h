/*

E-Ink display driver
    - ZJY128296-029EAAMFGN
    - Manufacturer: Zhongjingyuan
    - Size: 2.9 inch
    - Resolution: 128px x 296px
    - Flex connector label (not a unique identifier): FPC-A005 20.06.15 TRX

    Note: as of Feb. 2025, these panels are used for "WeActStudio 2.9in B&W" display modules

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class ZJY128296_029EAAMFGN : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 128;
    static constexpr uint32_t height = 296;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    ZJY128296_029EAAMFGN() : SSD16XX(width, height, supported) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS