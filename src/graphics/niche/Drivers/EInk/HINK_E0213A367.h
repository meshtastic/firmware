/*
    E-Ink display driver for Holitech HINK-E0213A367
    - 2.13 inch, 128x250, SSD1682 Controller
*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"
#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{

// Driver for the Holitech HINK-E0213A367, a 2.13-inch, 128x250 E-Ink display.
class HINK_E0213A367 : public SSD16XX
{
  private:
    // Display properties
    static constexpr uint32_t width = 128;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    HINK_E0213A367() : SSD16XX(width, height, supported) {}

  protected:
    // Overridden methods from SSD16XX base class
    void configFullscreen() override;
    void configScanning() override;
    void configWaveform() override;
    void configUpdateSequence() override;
    void detachFromUpdate() override;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
