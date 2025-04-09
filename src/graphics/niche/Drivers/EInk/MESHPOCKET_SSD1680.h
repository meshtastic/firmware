/*

E-Ink display driver
    - SSD1680
    - Manufacturer: DKE
    - Size: 2.13 inch
    - Resolution: 122px x 255px
    - Flex connector marking: FPC-7519 rev.b

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class MESHPOCKET_SSD1680 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
  MESHPOCKET_SSD1680() : SSD16XX(width, height, supported, 1) {} // Note: left edge of this display is offset by 1 byte

  protected:
  virtual void update(uint8_t *imageData, UpdateTypes type) override;
  virtual void configScanning() override;
  virtual void configWaveform() override;
  virtual void configUpdateSequence() override;
  void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS