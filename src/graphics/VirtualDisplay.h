#pragma once

#if ARCH_PORTDUINO

#include <OLEDDisplay.h>

namespace graphics
{

/**
 * A panel with no hardware behind it, for meshtasticd. BaseUI draws into the same 1bpp buffer it uses on an
 * OLED of this size, and the screen mirror is the only way to see it.
 */
class VirtualDisplay : public OLEDDisplay
{
  public:
    VirtualDisplay(uint16_t width, uint16_t height) { setGeometry(GEOMETRY_RAWMODE, width, height); }

    void display(void) override {}

  protected:
    int getBufferOffset(void) override { return 0; }
    bool connect() override { return true; }
    void sendCommand(uint8_t com) override { (void)com; }
};

} // namespace graphics

#endif
