/*

E-Ink display driver
    - SSD1682
    - Manufacturer: SEEKINK
    - Size: 2.13 inch
    - Resolution: 122px x 255px
    - Flex connector marking: HINK-E0213A162-A1 (hidden, printed on reverse)

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD1682.h"

namespace NicheGraphics::Drivers
{
class E0213A367 : public SSD1682
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    E0213A367() : SSD1682(width, height, supported, 0) {}

  protected:
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS