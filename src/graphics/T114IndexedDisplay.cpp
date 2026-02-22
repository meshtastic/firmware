#include "configuration.h"
#if HAS_SCREEN && defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114) && ENABLE_T114_INDEXED_UI

#include "T114IndexedDisplay.h"

#include "ColorOverlayQueue.h"
#include "ColorPalette.h"
#include <SPI.h>
#include <cstring>
#include <new>

namespace graphics
{

namespace
{
static constexpr uint8_t T114_CMD_CASET = 0x2A;
static constexpr uint8_t T114_CMD_RASET = 0x2B;
static constexpr uint8_t T114_CMD_RAMWR = 0x2C;
static ::SPISettings g_t114SpiSettings(40000000, MSBFIRST, SPI_MODE0);
} // namespace

T114IndexedDisplay::T114IndexedDisplay(SPIClass *spiClass, uint8_t rst, uint8_t dc, uint8_t cs, OLEDDISPLAY_GEOMETRY geometry,
                                       uint16_t width, uint16_t height, int mosi, int miso, int clk)
    : ST7789Spi(spiClass, rst, dc, cs, geometry, width, height, mosi, miso, clk)
{
    idxFront = new (std::nothrow) uint8_t[kPackedPixelBytes];
    idxBack = new (std::nothrow) uint8_t[kPackedPixelBytes];
    line565 = new (std::nothrow) uint16_t[TFT_WIDTH];

    if (idxFront != nullptr && idxBack != nullptr && line565 != nullptr) {
        memset(idxFront, (kUIPaletteBackground & 0x0F) | (kUIPaletteBackground << 4), kPackedPixelBytes);
        memset(idxBack, 0xFF, kPackedPixelBytes); // Force full redraw on first frame.
        resourcesReady = true;
        LOG_INFO("T114 indexed UI buffers allocated: %u bytes", static_cast<unsigned>(kPackedPixelBytes * 2 + (TFT_WIDTH * 2)));
    } else {
        delete[] idxFront;
        idxFront = nullptr;
        delete[] idxBack;
        idxBack = nullptr;
        delete[] line565;
        line565 = nullptr;
        resourcesReady = false;
        fullDirtyNextFrame = false;
        LOG_ERROR("T114 indexed UI disabled at runtime (buffer allocation failed), fallback to mono ST7789 path");
    }
}

T114IndexedDisplay::~T114IndexedDisplay()
{
    delete[] idxFront;
    delete[] idxBack;
    delete[] line565;
}

void T114IndexedDisplay::setAccentColor(uint16_t accent565)
{
    setUIPaletteAccent(accent565);
    paletteDirty = true;
    fullDirtyNextFrame = true;
}

bool T114IndexedDisplay::xbmBit(const uint8_t *xbm, uint16_t width, int16_t x, int16_t y)
{
    const int16_t widthInXbm = (width + 7) / 8;
    uint8_t data = pgm_read_byte(xbm + (x / 8) + y * widthInXbm);
    data >>= (x & 7);
    return (data & 0x01) != 0;
}

inline uint8_t T114IndexedDisplay::getPackedPixel(const uint8_t *buf, uint32_t pixel)
{
    const uint8_t packed = buf[pixel >> 1];
    return (pixel & 1u) ? (packed >> 4) : (packed & 0x0F);
}

inline void T114IndexedDisplay::setPackedPixel(uint8_t *buf, uint32_t pixel, uint8_t value)
{
    const uint32_t byteIndex = pixel >> 1;
    const uint8_t val = value & 0x0F;
    if (pixel & 1u) {
        buf[byteIndex] = static_cast<uint8_t>((buf[byteIndex] & 0x0F) | (val << 4));
    } else {
        buf[byteIndex] = static_cast<uint8_t>((buf[byteIndex] & 0xF0) | val);
    }
}

void T114IndexedDisplay::rebuildPaletteIfNeeded()
{
    const uint16_t accent = getUIPaletteAccent();
    if (!paletteDirty && accent == lastAccent565) {
        return;
    }

    lastAccent565 = accent;
    fillUIPalette565(palette565, kPaletteSize);
    paletteDirty = false;
    fullDirtyNextFrame = true;
}

void T114IndexedDisplay::composeMonoLayer()
{
    const int16_t w = static_cast<int16_t>(displayWidth);
    const int16_t h = static_cast<int16_t>(displayHeight);

    for (int16_t y = 0; y < h; ++y) {
        const uint32_t rowOffset = static_cast<uint32_t>(y) * w;
        const uint32_t yByteIndex = (static_cast<uint32_t>(y) / 8) * w;
        const uint8_t yByteMask = static_cast<uint8_t>(1u << (y & 7));

        for (int16_t x = 0; x < w; ++x) {
            const bool bitSet = (buffer[static_cast<uint32_t>(x) + yByteIndex] & yByteMask) != 0;
            setPackedPixel(idxFront, rowOffset + static_cast<uint32_t>(x), bitSet ? kUIPaletteForeground : kUIPaletteBackground);
        }
    }
}

bool T114IndexedDisplay::overlayBounds(const ColorOverlay &overlay, int16_t &left, int16_t &top, int16_t &right, int16_t &bottom) const
{
    if (overlay.width == 0 || overlay.height == 0) {
        return false;
    }

    const int16_t screenRight = static_cast<int16_t>(displayWidth) - 1;
    const int16_t screenBottom = static_cast<int16_t>(displayHeight) - 1;

    int16_t l = overlay.x;
    int16_t t = overlay.y;
    int16_t r = static_cast<int16_t>(overlay.x + static_cast<int16_t>(overlay.width) - 1);
    int16_t b = static_cast<int16_t>(overlay.y + static_cast<int16_t>(overlay.height) - 1);

    if (l < overlay.clipLeft)
        l = overlay.clipLeft;
    if (t < overlay.clipTop)
        t = overlay.clipTop;
    if (r > overlay.clipRight)
        r = overlay.clipRight;
    if (b > overlay.clipBottom)
        b = overlay.clipBottom;

    if (l < 0)
        l = 0;
    if (t < 0)
        t = 0;
    if (r > screenRight)
        r = screenRight;
    if (b > screenBottom)
        b = screenBottom;

    if (r < l || b < t) {
        return false;
    }

    left = l;
    top = t;
    right = r;
    bottom = b;
    return true;
}

void T114IndexedDisplay::applyQueuedOverlays(const ColorOverlay *overlays, uint8_t count)
{
    if (overlays == nullptr || count == 0) {
        return;
    }

    const int16_t w = static_cast<int16_t>(displayWidth);

    for (uint8_t i = 0; i < count; ++i) {
        const ColorOverlay &overlay = overlays[i];

        int16_t left = 0, top = 0, right = -1, bottom = -1;
        if (!overlayBounds(overlay, left, top, right, bottom)) {
            continue;
        }

        if (overlay.type == ColorOverlayType::Rect) {
            for (int16_t y = top; y <= bottom; ++y) {
                const uint32_t rowOffset = static_cast<uint32_t>(y) * w;
                for (int16_t x = left; x <= right; ++x) {
                    setPackedPixel(idxFront, rowOffset + static_cast<uint32_t>(x), overlay.paletteIndex);
                }
            }
            continue;
        }

        if (overlay.xbm == nullptr) {
            continue;
        }

        for (int16_t srcY = 0; srcY < static_cast<int16_t>(overlay.height); ++srcY) {
            const int16_t y = overlay.y + srcY;
            if (y < top || y > bottom) {
                continue;
            }

            const uint32_t rowOffset = static_cast<uint32_t>(y) * w;

            for (int16_t srcX = 0; srcX < static_cast<int16_t>(overlay.width); ++srcX) {
                const int16_t x = overlay.x + srcX;
                if (x < left || x > right) {
                    continue;
                }
                if (!xbmBit(overlay.xbm, overlay.width, srcX, srcY)) {
                    continue;
                }

                setPackedPixel(idxFront, rowOffset + static_cast<uint32_t>(x), overlay.paletteIndex);
            }
        }
    }
}

bool T114IndexedDisplay::overlaysEqual(const ColorOverlay *a, uint8_t aCount, const ColorOverlay *b, uint8_t bCount)
{
    if (aCount != bCount) {
        return false;
    }
    for (uint8_t i = 0; i < aCount; ++i) {
        if (a[i].type != b[i].type || a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width || a[i].height != b[i].height ||
            a[i].xbm != b[i].xbm || a[i].paletteIndex != b[i].paletteIndex || a[i].clipLeft != b[i].clipLeft ||
            a[i].clipTop != b[i].clipTop || a[i].clipRight != b[i].clipRight || a[i].clipBottom != b[i].clipBottom) {
            return false;
        }
    }
    return true;
}

void T114IndexedDisplay::mergeDirtyRect(int16_t left, int16_t top, int16_t right, int16_t bottom, int16_t &dirtyLeft, int16_t &dirtyTop,
                                        int16_t &dirtyRight, int16_t &dirtyBottom, bool &hasDirty)
{
    if (right < left || bottom < top) {
        return;
    }

    if (!hasDirty) {
        dirtyLeft = left;
        dirtyTop = top;
        dirtyRight = right;
        dirtyBottom = bottom;
        hasDirty = true;
        return;
    }

    if (left < dirtyLeft)
        dirtyLeft = left;
    if (top < dirtyTop)
        dirtyTop = top;
    if (right > dirtyRight)
        dirtyRight = right;
    if (bottom > dirtyBottom)
        dirtyBottom = bottom;
}

void T114IndexedDisplay::stWriteCommand(uint8_t c)
{
    digitalWrite(ST7789_RS, LOW);
    ::SPI1.transfer(c);
    digitalWrite(ST7789_RS, HIGH);
}

void T114IndexedDisplay::stWriteData16(uint16_t v)
{
    ::SPI1.transfer(static_cast<uint8_t>(v >> 8));
    ::SPI1.transfer(static_cast<uint8_t>(v & 0xFF));
}

void T114IndexedDisplay::stSetAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    x += (320 - TFT_WIDTH) / 2;
    y += (240 - TFT_HEIGHT) / 2;

    const uint16_t x2 = static_cast<uint16_t>(x + w - 1);
    const uint16_t y2 = static_cast<uint16_t>(y + h - 1);

    stWriteCommand(T114_CMD_CASET);
    stWriteData16(x);
    stWriteData16(x2);

    stWriteCommand(T114_CMD_RASET);
    stWriteData16(y);
    stWriteData16(y2);

    stWriteCommand(T114_CMD_RAMWR);
}

void T114IndexedDisplay::display(void)
{
    if (!resourcesReady) {
        // Keep overlay state in sync even when falling back to legacy renderer.
        finishColorOverlayFrame();
        ST7789Spi::display();
        return;
    }

    if (!initLogged) {
        LOG_INFO("T114 indexed UI active");
        initLogged = true;
    }

    rebuildPaletteIfNeeded();

    if (fullDirtyNextFrame) {
        memset(idxBack, 0xFF, kPackedPixelBytes);
    }

    composeMonoLayer();

    uint8_t currentCount = 0;
    uint8_t previousCount = 0;
    const ColorOverlay *currentOverlays = getCurrentColorOverlays(currentCount);
    const ColorOverlay *previousOverlays = getPreviousColorOverlays(previousCount);
    applyQueuedOverlays(currentOverlays, currentCount);

    const bool overlaysChanged = !overlaysEqual(currentOverlays, currentCount, previousOverlays, previousCount);

    int16_t dirtyLeft = static_cast<int16_t>(displayWidth);
    int16_t dirtyTop = static_cast<int16_t>(displayHeight);
    int16_t dirtyRight = -1;
    int16_t dirtyBottom = -1;
    bool hasDirty = false;

    if (fullDirtyNextFrame) {
        mergeDirtyRect(0, 0, static_cast<int16_t>(displayWidth) - 1, static_cast<int16_t>(displayHeight) - 1, dirtyLeft, dirtyTop,
                       dirtyRight, dirtyBottom, hasDirty);
    }

#ifdef OLEDDISPLAY_DOUBLE_BUFFER
    const int16_t pageCount = static_cast<int16_t>((displayHeight + 7) / 8);
    for (int16_t page = 0; page < pageCount; ++page) {
        const uint32_t pageOffset = static_cast<uint32_t>(page) * displayWidth;
        for (int16_t x = 0; x < static_cast<int16_t>(displayWidth); ++x) {
            const uint32_t idx = pageOffset + static_cast<uint32_t>(x);
            if (buffer[idx] == buffer_back[idx]) {
                continue;
            }
            const int16_t yTop = static_cast<int16_t>(page * 8);
            int16_t yBottom = static_cast<int16_t>(yTop + 7);
            if (yBottom >= static_cast<int16_t>(displayHeight)) {
                yBottom = static_cast<int16_t>(displayHeight) - 1;
            }
            mergeDirtyRect(x, yTop, x, yBottom, dirtyLeft, dirtyTop, dirtyRight, dirtyBottom, hasDirty);
        }
    }
#else
    mergeDirtyRect(0, 0, static_cast<int16_t>(displayWidth) - 1, static_cast<int16_t>(displayHeight) - 1, dirtyLeft, dirtyTop, dirtyRight,
                   dirtyBottom, hasDirty);
#endif

    if (overlaysChanged) {
        for (uint8_t i = 0; i < previousCount; ++i) {
            int16_t l = 0, t = 0, r = -1, b = -1;
            if (overlayBounds(previousOverlays[i], l, t, r, b)) {
                mergeDirtyRect(l, t, r, b, dirtyLeft, dirtyTop, dirtyRight, dirtyBottom, hasDirty);
            }
        }

        for (uint8_t i = 0; i < currentCount; ++i) {
            int16_t l = 0, t = 0, r = -1, b = -1;
            if (overlayBounds(currentOverlays[i], l, t, r, b)) {
                mergeDirtyRect(l, t, r, b, dirtyLeft, dirtyTop, dirtyRight, dirtyBottom, hasDirty);
            }
        }
    }

    if (hasDirty) {
        ::SPI1.beginTransaction(g_t114SpiSettings);
        digitalWrite(ST7789_NSS, LOW);
        digitalWrite(ST7789_RS, HIGH);

        const int16_t w = static_cast<int16_t>(displayWidth);

        for (int16_t y = dirtyTop; y <= dirtyBottom; ++y) {
            const uint32_t rowOffset = static_cast<uint32_t>(y) * w;
            if (fullDirtyNextFrame) {
                const uint16_t runLength = static_cast<uint16_t>(dirtyRight - dirtyLeft + 1);
                for (uint16_t i = 0; i < runLength; ++i) {
                    const uint32_t pixel = rowOffset + static_cast<uint32_t>(dirtyLeft + i);
                    const uint8_t idx = getPackedPixel(idxFront, pixel);
                    setPackedPixel(idxBack, pixel, idx);
                    line565[i] = palette565[idx];
                }
                stSetAddrWindow(static_cast<uint16_t>(dirtyLeft), static_cast<uint16_t>(y), runLength, 1);
                for (uint16_t i = 0; i < runLength; ++i) {
                    stWriteData16(line565[i]);
                }
            } else {
                int16_t x = dirtyLeft;
                while (x <= dirtyRight) {
                    const uint32_t pixel = rowOffset + static_cast<uint32_t>(x);
                    if (getPackedPixel(idxFront, pixel) == getPackedPixel(idxBack, pixel)) {
                        ++x;
                        continue;
                    }

                    const int16_t runStart = x;
                    uint16_t runLength = 0;
                    while (x <= dirtyRight) {
                        const uint32_t runPixel = rowOffset + static_cast<uint32_t>(x);
                        const uint8_t frontIdx = getPackedPixel(idxFront, runPixel);
                        const uint8_t backIdx = getPackedPixel(idxBack, runPixel);
                        if (frontIdx == backIdx) {
                            break;
                        }
                        setPackedPixel(idxBack, runPixel, frontIdx);
                        line565[runLength++] = palette565[frontIdx];
                        ++x;
                    }

                    stSetAddrWindow(static_cast<uint16_t>(runStart), static_cast<uint16_t>(y), runLength, 1);
                    for (uint16_t i = 0; i < runLength; ++i) {
                        stWriteData16(line565[i]);
                    }
                }
            }
        }

        digitalWrite(ST7789_NSS, HIGH);
        ::SPI1.endTransaction();
    }

#ifdef OLEDDISPLAY_DOUBLE_BUFFER
    if (hasDirty) {
        memcpy(buffer_back, buffer, displayBufferSize);
    }
#endif

    finishColorOverlayFrame();
    fullDirtyNextFrame = false;
}

} // namespace graphics

#endif
