/*

E-Ink base class for displays based on SSD1682

SSD1682 has a few quirks. We're implementing them here in a new base class,
to avoid re-implementing them every time we need to add a new SSD1682-based display.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./SSD16XX.h"

namespace NicheGraphics::Drivers
{

class SSD1682 : public SSD16XX
{
  public:
    SSD1682(uint16_t width, uint16_t height, EInk::UpdateTypes supported, uint8_t bufferOffsetX = 0);
    virtual void configFullscreen(); // Select memory region on controller IC
    virtual void deepSleep() {}      // Not usable (image memory not retained)
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS