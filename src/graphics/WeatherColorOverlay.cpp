#include "configuration.h"
#if HAS_SCREEN

#include "WeatherColorOverlay.h"

#if defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114)
#include <SPI.h>
#endif

namespace graphics
{

#if defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114)

namespace
{
struct WeatherOverlay {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    const uint8_t *xbm;
    uint16_t color565;
};

static constexpr uint8_t MAX_WEATHER_OVERLAYS = 40;
static WeatherOverlay g_overlays[MAX_WEATHER_OVERLAYS];
static uint8_t g_overlayCount = 0;
static int16_t g_clipLeft = 0;
static int16_t g_clipTop = 0;
static int16_t g_clipRight = TFT_WIDTH - 1;
static int16_t g_clipBottom = TFT_HEIGHT - 1;

static constexpr uint8_t ST77XX_CASET = 0x2A;
static constexpr uint8_t ST77XX_RASET = 0x2B;
static constexpr uint8_t ST77XX_RAMWR = 0x2C;

// Keep this aligned with ST7789Spi default.
static ::SPISettings g_overlaySpiSettings(40000000, MSBFIRST, SPI_MODE0);

static inline bool xbmBit(const uint8_t *xbm, uint16_t width, int16_t x, int16_t y)
{
    const int16_t widthInXbm = (width + 7) / 8;
    uint8_t data = pgm_read_byte(xbm + (x / 8) + y * widthInXbm);
    data >>= (x & 7);
    return (data & 0x01) != 0;
}

static inline void stWriteCommand(uint8_t c)
{
    digitalWrite(ST7789_RS, LOW);
    ::SPI1.transfer(c);
    digitalWrite(ST7789_RS, HIGH);
}

static inline void stWriteData16(uint16_t v)
{
    ::SPI1.transfer(static_cast<uint8_t>(v >> 8));
    ::SPI1.transfer(static_cast<uint8_t>(v & 0xFF));
}

static inline void stSetAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    // Must match the centering used by ST7789Spi::setAddrWindow().
    x += (320 - TFT_WIDTH) / 2;
    y += (240 - TFT_HEIGHT) / 2;

    const uint16_t x2 = static_cast<uint16_t>(x + w - 1);
    const uint16_t y2 = static_cast<uint16_t>(y + h - 1);

    stWriteCommand(ST77XX_CASET);
    stWriteData16(x);
    stWriteData16(x2);

    stWriteCommand(ST77XX_RASET);
    stWriteData16(y);
    stWriteData16(y2);

    stWriteCommand(ST77XX_RAMWR);
}

static void drawXbmColorTransparent(const WeatherOverlay &o)
{
    if (o.xbm == nullptr || o.width == 0 || o.height == 0) {
        return;
    }

    if (o.x >= TFT_WIDTH || o.y >= TFT_HEIGHT) {
        return;
    }

    for (int16_t row = 0; row < static_cast<int16_t>(o.height); ++row) {
        const int16_t y = o.y + row;
        if (y < 0 || y >= TFT_HEIGHT || y < g_clipTop || y > g_clipBottom) {
            continue;
        }

        int16_t runStart = -1;
        for (int16_t col = 0; col < static_cast<int16_t>(o.width); ++col) {
            const bool bitSet = xbmBit(o.xbm, o.width, col, row);
            if (bitSet) {
                if (runStart < 0) {
                    runStart = col;
                }
            } else if (runStart >= 0) {
                const int16_t runEnd = col - 1;
                const int16_t x1 = o.x + runStart;
                const int16_t x2 = o.x + runEnd;
                if (x2 >= 0 && x1 < TFT_WIDTH) {
                    int16_t clippedX1 = (x1 < 0) ? 0 : x1;
                    int16_t clippedX2 = (x2 >= TFT_WIDTH) ? (TFT_WIDTH - 1) : x2;
                    if (clippedX1 < g_clipLeft)
                        clippedX1 = g_clipLeft;
                    if (clippedX2 > g_clipRight)
                        clippedX2 = g_clipRight;
                    const uint16_t runLen = static_cast<uint16_t>(clippedX2 - clippedX1 + 1);
                    if (runLen > 0) {
                        stSetAddrWindow(static_cast<uint16_t>(clippedX1), static_cast<uint16_t>(y), runLen, 1);
                        for (uint16_t i = 0; i < runLen; ++i) {
                            stWriteData16(o.color565);
                        }
                    }
                }
                runStart = -1;
            }
        }

        if (runStart >= 0) {
            const int16_t x1 = o.x + runStart;
            const int16_t x2 = o.x + static_cast<int16_t>(o.width) - 1;
            if (x2 >= 0 && x1 < TFT_WIDTH) {
                int16_t clippedX1 = (x1 < 0) ? 0 : x1;
                int16_t clippedX2 = (x2 >= TFT_WIDTH) ? (TFT_WIDTH - 1) : x2;
                if (clippedX1 < g_clipLeft)
                    clippedX1 = g_clipLeft;
                if (clippedX2 > g_clipRight)
                    clippedX2 = g_clipRight;
                const uint16_t runLen = static_cast<uint16_t>(clippedX2 - clippedX1 + 1);
                if (runLen > 0) {
                    stSetAddrWindow(static_cast<uint16_t>(clippedX1), static_cast<uint16_t>(y), runLen, 1);
                    for (uint16_t i = 0; i < runLen; ++i) {
                        stWriteData16(o.color565);
                    }
                }
            }
        }
    }
}
} // namespace

void setWeatherColorOverlayClip(int16_t left, int16_t top, int16_t right, int16_t bottom)
{
    if (left > right || top > bottom) {
        g_clipLeft = 0;
        g_clipTop = 0;
        g_clipRight = TFT_WIDTH - 1;
        g_clipBottom = TFT_HEIGHT - 1;
        return;
    }

    g_clipLeft = (left < 0) ? 0 : left;
    g_clipTop = (top < 0) ? 0 : top;
    g_clipRight = (right >= TFT_WIDTH) ? (TFT_WIDTH - 1) : right;
    g_clipBottom = (bottom >= TFT_HEIGHT) ? (TFT_HEIGHT - 1) : bottom;
}

void clearWeatherColorOverlays()
{
    g_overlayCount = 0;
    g_clipLeft = 0;
    g_clipTop = 0;
    g_clipRight = TFT_WIDTH - 1;
    g_clipBottom = TFT_HEIGHT - 1;
}

void queueWeatherColorOverlay(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t *xbm, uint16_t color565)
{
    if (xbm == nullptr || width == 0 || height == 0) {
        return;
    }
    if (g_overlayCount >= MAX_WEATHER_OVERLAYS) {
        return;
    }

    WeatherOverlay item;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;
    item.xbm = xbm;
    item.color565 = color565;
    g_overlays[g_overlayCount++] = item;
}

void flushWeatherColorOverlays()
{
    if (g_overlayCount == 0) {
        return;
    }

    ::SPI1.beginTransaction(g_overlaySpiSettings);
    digitalWrite(ST7789_NSS, LOW);

    for (uint8_t i = 0; i < g_overlayCount; ++i) {
        drawXbmColorTransparent(g_overlays[i]);
    }

    digitalWrite(ST7789_NSS, HIGH);
    ::SPI1.endTransaction();

    g_overlayCount = 0;
}

#else

void setWeatherColorOverlayClip(int16_t, int16_t, int16_t, int16_t) {}

void clearWeatherColorOverlays() {}

void queueWeatherColorOverlay(int16_t, int16_t, uint16_t, uint16_t, const uint8_t *, uint16_t) {}

void flushWeatherColorOverlays() {}

#endif

} // namespace graphics

#endif
