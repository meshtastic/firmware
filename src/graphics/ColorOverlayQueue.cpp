#include "configuration.h"
#if HAS_SCREEN

#include "ColorOverlayQueue.h"

namespace graphics
{

#if defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114) && ENABLE_T114_INDEXED_UI

namespace
{
static constexpr uint8_t MAX_COLOR_OVERLAYS = 80;

static ColorOverlay g_currentOverlays[MAX_COLOR_OVERLAYS];
static uint8_t g_currentOverlayCount = 0;
static ColorOverlay g_previousOverlays[MAX_COLOR_OVERLAYS];
static uint8_t g_previousOverlayCount = 0;

static int16_t g_clipLeft = 0;
static int16_t g_clipTop = 0;
static int16_t g_clipRight = TFT_WIDTH - 1;
static int16_t g_clipBottom = TFT_HEIGHT - 1;
} // namespace

void setColorOverlayClip(int16_t left, int16_t top, int16_t right, int16_t bottom)
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

void clearColorOverlays()
{
    g_currentOverlayCount = 0;
    g_clipLeft = 0;
    g_clipTop = 0;
    g_clipRight = TFT_WIDTH - 1;
    g_clipBottom = TFT_HEIGHT - 1;
}

static void queueColorOverlayInternal(ColorOverlayType type, int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t *xbm,
                                      uint8_t paletteIndex)
{
    if (width == 0 || height == 0) {
        return;
    }
    if (type == ColorOverlayType::Xbm && xbm == nullptr) {
        return;
    }
    if (g_currentOverlayCount >= MAX_COLOR_OVERLAYS) {
        return;
    }

    ColorOverlay item;
    item.type = type;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;
    item.xbm = xbm;
    item.paletteIndex = paletteIndex;
    item.clipLeft = g_clipLeft;
    item.clipTop = g_clipTop;
    item.clipRight = g_clipRight;
    item.clipBottom = g_clipBottom;

    g_currentOverlays[g_currentOverlayCount++] = item;
}

void queueColorOverlayXbm(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t *xbm, uint8_t paletteIndex)
{
    queueColorOverlayInternal(ColorOverlayType::Xbm, x, y, width, height, xbm, paletteIndex);
}

void queueColorOverlayRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint8_t paletteIndex)
{
    queueColorOverlayInternal(ColorOverlayType::Rect, x, y, width, height, nullptr, paletteIndex);
}

const ColorOverlay *getCurrentColorOverlays(uint8_t &count)
{
    count = g_currentOverlayCount;
    return g_currentOverlays;
}

const ColorOverlay *getPreviousColorOverlays(uint8_t &count)
{
    count = g_previousOverlayCount;
    return g_previousOverlays;
}

void finishColorOverlayFrame()
{
    g_previousOverlayCount = g_currentOverlayCount;
    for (uint8_t i = 0; i < g_currentOverlayCount; ++i) {
        g_previousOverlays[i] = g_currentOverlays[i];
    }

    g_currentOverlayCount = 0;
    g_clipLeft = 0;
    g_clipTop = 0;
    g_clipRight = TFT_WIDTH - 1;
    g_clipBottom = TFT_HEIGHT - 1;
}

#else

void setColorOverlayClip(int16_t, int16_t, int16_t, int16_t) {}
void clearColorOverlays() {}
void queueColorOverlayXbm(int16_t, int16_t, uint16_t, uint16_t, const uint8_t *, uint8_t) {}
void queueColorOverlayRect(int16_t, int16_t, uint16_t, uint16_t, uint8_t) {}

const ColorOverlay *getCurrentColorOverlays(uint8_t &count)
{
    count = 0;
    return nullptr;
}

const ColorOverlay *getPreviousColorOverlays(uint8_t &count)
{
    count = 0;
    return nullptr;
}

void finishColorOverlayFrame() {}

#endif

} // namespace graphics

#endif
