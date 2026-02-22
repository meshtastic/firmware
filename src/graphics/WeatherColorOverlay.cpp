#include "configuration.h"
#if HAS_SCREEN

#include "WeatherColorOverlay.h"

#if defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114) && ENABLE_T114_INDEXED_UI
#include "ColorOverlayQueue.h"
#include "ColorPalette.h"
#endif

namespace graphics
{

#if defined(USE_ST7789) && defined(HELTEC_MESH_NODE_T114) && ENABLE_T114_INDEXED_UI

void setWeatherColorOverlayClip(int16_t left, int16_t top, int16_t right, int16_t bottom)
{
    setColorOverlayClip(left, top, right, bottom);
}

void clearWeatherColorOverlays()
{
    clearColorOverlays();
}

void queueWeatherColorOverlay(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t *xbm, uint16_t color565)
{
    const uint8_t paletteIndex = mapWeatherColor565ToPaletteIndex(color565);
    queueColorOverlayXbm(x, y, width, height, xbm, paletteIndex);
}

// Color overlays are flushed by T114IndexedDisplay::display().
void flushWeatherColorOverlays() {}

#else

void setWeatherColorOverlayClip(int16_t, int16_t, int16_t, int16_t) {}
void clearWeatherColorOverlays() {}
void queueWeatherColorOverlay(int16_t, int16_t, uint16_t, uint16_t, const uint8_t *, uint16_t) {}
void flushWeatherColorOverlays() {}

#endif

} // namespace graphics

#endif
