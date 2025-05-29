/*

E-Ink display driver
    - SSD1682
    - Manufacturer: WISEVAST
    - Size: 2.13 inch
    - Resolution: 122px x 255px
    - Flex connector marking: HINK-E0213A162-FPC-A0 (Hidden, printed on back-side)

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{
class E0213A367 : public SSD16XX
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    E0213A367() : SSD16XX(width, height, supported, 0) {}

  protected:
    virtual void configScanning() override;
    virtual void configWaveform() override;
    virtual void configUpdateSequence() override;
    virtual void detachFromUpdate() override;
    virtual void configFullscreen() override;
    virtual void deepSleep() override;
    virtual void finalizeUpdate() override;
};

} // namespace NicheGraphics::Drivers
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS