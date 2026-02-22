#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114) && ENABLE_T114_INDEXED_UI

#include <ST7789Spi.h>

namespace graphics
{

class T114IndexedDisplay final : public ST7789Spi
{
  public:
    T114IndexedDisplay(SPIClass *spiClass, uint8_t rst, uint8_t dc, uint8_t cs, OLEDDISPLAY_GEOMETRY geometry = GEOMETRY_RAWMODE,
                       uint16_t width = 240, uint16_t height = 135, int mosi = -1, int miso = -1, int clk = -1);
    ~T114IndexedDisplay() override;

    void display(void) override;
    void setAccentColor(uint16_t accent565);

  private:
    static constexpr uint8_t kPaletteSize = 16;
    static constexpr uint32_t kPixelCount = static_cast<uint32_t>(TFT_WIDTH) * static_cast<uint32_t>(TFT_HEIGHT);
    static constexpr uint32_t kPackedPixelBytes = (kPixelCount + 1u) / 2u;

    uint8_t *idxFront = nullptr;
    uint8_t *idxBack = nullptr;
    uint16_t palette565[kPaletteSize];
    uint16_t *line565 = nullptr;

    uint16_t lastAccent565 = 0;
    bool resourcesReady = false;
    bool initLogged = false;
    bool paletteDirty = true;
    bool fullDirtyNextFrame = true;

    static bool xbmBit(const uint8_t *xbm, uint16_t width, int16_t x, int16_t y);
    static inline uint8_t getPackedPixel(const uint8_t *buf, uint32_t pixel);
    static inline void setPackedPixel(uint8_t *buf, uint32_t pixel, uint8_t value);
    void rebuildPaletteIfNeeded();
    void composeMonoLayer();
    void applyQueuedOverlays(const class ColorOverlay *overlays, uint8_t count);
    bool overlayBounds(const class ColorOverlay &overlay, int16_t &left, int16_t &top, int16_t &right, int16_t &bottom) const;
    static bool overlaysEqual(const class ColorOverlay *a, uint8_t aCount, const class ColorOverlay *b, uint8_t bCount);
    static void mergeDirtyRect(int16_t left, int16_t top, int16_t right, int16_t bottom, int16_t &dirtyLeft, int16_t &dirtyTop,
                               int16_t &dirtyRight, int16_t &dirtyBottom, bool &hasDirty);

    static void stWriteCommand(uint8_t c);
    static void stWriteData16(uint16_t v);
    static void stSetAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
};

} // namespace graphics

#endif
