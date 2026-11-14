#pragma once

#if ARCH_PORTDUINO

#include "configuration.h"
#include "graphics/ScreenMirror.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <OLEDDisplay.h>

namespace graphics
{

/**
 * A panel with no hardware behind it, for meshtasticd. BaseUI draws into the same 1bpp buffer it uses on a
 * physical panel of this size, and the screen mirror is the only way to see it.
 */
class VirtualDisplay : public OLEDDisplay
{
  public:
    VirtualDisplay(uint16_t width, uint16_t height) { setGeometry(GEOMETRY_RAWMODE, width, height); }

    /// The panel class of the devices this build's BaseUI profile renders.
    static meshtastic_DisplayInfo_PanelClass panelClass()
    {
        if (BASEUI_EINK_LAYOUT)
            return meshtastic_DisplayInfo_PanelClass_EINK;
        if (GRAPHICS_TFT_COLORING_ENABLED)
            return meshtastic_DisplayInfo_PanelClass_TFT;
        return meshtastic_DisplayInfo_PanelClass_OLED;
    }

    void display(void) override
    {
#if GRAPHICS_TFT_COLORING_ENABLED
        // what TFTDisplay hands the mirror before it clears the frame's color regions
#if HAS_SCREEN_MIRROR
        screenMirror.capturePalette(getTFTColorFrameSignature(), swapBytes(TFTPalette::White), swapBytes(getThemeBodyBg()),
                                    colorRegions, getTFTColorRegionCount());
#endif
        clearTFTColorRegions();
#endif
    }

  protected:
    int getBufferOffset(void) override { return 0; }
    bool connect() override { return true; }
    void sendCommand(uint8_t com) override { (void)com; }

  private:
    static uint16_t swapBytes(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
};

} // namespace graphics

#endif
