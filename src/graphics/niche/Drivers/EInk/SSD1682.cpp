#include "./SSD1682.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

SSD1682::SSD1682(uint16_t width, uint16_t height, EInk::UpdateTypes supported, uint8_t bufferOffsetX)
    : SSD16XX(width, height, supported, bufferOffsetX)
{
}

// SSD1682 only accepts single-byte x and y values
// This causes an incompatibility with the default SSD16XX::configFullscreen
void SSD1682::configFullscreen()
{
    // Define the boundaries of the "fullscreen" region, for the controller IC
    static const uint8_t sx = bufferOffsetX; // Notice the offset
    static const uint8_t sy = 0;
    static const uint8_t ex = bufferRowSize + bufferOffsetX - 1; // End is "max index", not "count". Minus 1 handles this
    static const uint8_t ey = height;

    // Data entry mode - Left to Right, Top to Bottom
    sendCommand(0x11);
    sendData(0x03);

    // Select controller IC memory region to display a fullscreen image
    sendCommand(0x44); // Memory X start - end
    sendData(sx);
    sendData(ex);
    sendCommand(0x45); // Memory Y start - end
    sendData(sy);
    sendData(ey);

    // Place the cursor at the start of this memory region, ready to send image data x=0 y=0
    sendCommand(0x4E); // Memory cursor X
    sendData(sx);
    sendCommand(0x4F); // Memory cursor y
    sendData(sy);
}

#endif