#include "configuration.h"

#if defined(USE_HUB75)

#include "HUB75Display.h"
#include "TFTColorRegions.h"
#include "TFTPalette.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <string.h>

HUB75Display::HUB75Display(uint8_t, int, int, OLEDDISPLAY_GEOMETRY, HW_I2C)
{
    // The BaseUI treats the panel as a generic raw framebuffer (not an SSD1306
    // page layout). Geometry is fixed by the wired panel size.
#if defined(SCREEN_ROTATE)
    setGeometry(GEOMETRY_RAWMODE, TFT_HEIGHT, TFT_WIDTH);
#else
    setGeometry(GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
    LOG_DEBUG("HUB75Display %dx%d", (int)TFT_WIDTH, (int)TFT_HEIGHT);
}

HUB75Display::~HUB75Display()
{
    if (matrix) {
        delete matrix;
        matrix = nullptr;
    }
}

// Bring up the matrix DMA driver from the wired HUB75 pins.
bool HUB75Display::connect()
{
    LOG_INFO("Do HUB75 init");

    HUB75_I2S_CFG::i2s_pins pins = {HUB75_R1, HUB75_G1, HUB75_B1, HUB75_R2, HUB75_G2,  HUB75_B2, HUB75_A,
                                    HUB75_B,  HUB75_C,  HUB75_D,  HUB75_E,  HUB75_LAT, HUB75_OE, HUB75_CLK};

    HUB75_I2S_CFG cfg(TFT_WIDTH, TFT_HEIGHT, 1 /* chain length */, pins);

    cfg.i2sspeed = HUB75_I2S_CFG::HZ_8M; // timing headroom (signal integrity)
    cfg.latch_blanking = 4;              // clean row transitions
    cfg.clkphase = false;                // inverted clock phase: fixes 1px skew of lower half
    cfg.double_buff = false;             // single buffer: halves the internal-SRAM DMA
                                         // footprint. display() draws directly into the
                                         // live buffer with a dirty-diff

    matrix = new MatrixPanel_I2S_DMA(cfg);
    bool ok = matrix->begin();
    if (!ok) {
        LOG_ERROR("HUB75 matrix->begin() failed (DMA buffer alloc?)");
        return false;
    }
    matrix->setBrightness8(brightness);
    matrix->clearScreen();
    return true;
}

void HUB75Display::display()
{
    if (!matrix)
        return;

    const uint16_t onNative = graphics::TFTPalette::White;
    const uint16_t offNative = graphics::getThemeBodyBg();


    const uint16_t onBe = (uint16_t)((onNative >> 8) | (onNative << 8));
    const uint16_t offBe = (uint16_t)((offNative >> 8) | (offNative << 8));

    bool forceFull = firstFrame || !haveThemeDefaults || onBe != lastOnBe || offBe != lastOffBe;

#if GRAPHICS_TFT_COLORING_ENABLED
    const bool hasColorRegions = graphics::getTFTColorRegionCount() > 0;
    const uint32_t colorSig = graphics::getTFTColorFrameSignature();
    if (colorSig != lastColorSig)
        forceFull = true;
#endif

    for (uint16_t y = 0; y < displayHeight; y++) {
        const uint32_t yByteIndex = (y / 8) * displayWidth;
        const uint8_t yByteMask = (uint8_t)(1 << (y & 7));

#if GRAPHICS_TFT_COLORING_ENABLED
        if (hasColorRegions)
            graphics::beginTFTColorRow((int16_t)y);
#endif

        for (uint16_t x = 0; x < displayWidth; x++) {
            const bool isset = (buffer[x + yByteIndex] & yByteMask) != 0;

            // Skip pixels whose mono bit is unchanged (unless a full repaint is
            // forced). The panel is single-buffered, so untouched pixels persist.
            if (!forceFull && (((buffer_back[x + yByteIndex] & yByteMask) != 0) == isset))
                continue;

            uint16_t be;
#if GRAPHICS_TFT_COLORING_ENABLED
            if (hasColorRegions)
                be = graphics::resolveTFTColorPixelRow((int16_t)x, isset, onBe, offBe);
            else
                be = isset ? onBe : offBe;
#else
            be = isset ? onBe : offBe;
#endif

            const uint16_t c = (uint16_t)((be >> 8) | (be << 8)); // back to native RGB565
            const uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
            const uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
            const uint8_t b = (uint8_t)((c & 0x1F) << 3);
            matrix->drawPixelRGB888(x, y, r, g, b);
        }
    }

    // Remember what the panel now shows so the next frame can diff against it.
    memcpy(buffer_back, buffer, displayBufferSize);

    haveThemeDefaults = true;
    lastOnBe = onBe;
    lastOffBe = offBe;
    firstFrame = false;
#if GRAPHICS_TFT_COLORING_ENABLED
    lastColorSig = colorSig;
    // Regions are re-registered every frame by the renderers; clear so they
    // don't accumulate across frames.
    graphics::clearTFTColorRegions();
#endif
}

void HUB75Display::sendCommand(uint8_t com)
{
    if (!matrix)
        return;

    switch (com) {
    case DISPLAYON:
        matrix->setBrightness8(brightness);
        break;
    case DISPLAYOFF:
        matrix->setBrightness8(0);
        break;
    default:
        // Drop all other SSD1306 init/config commands - not meaningful for the matrix.
        break;
    }
}

void HUB75Display::setDisplayBrightness(uint8_t _brightness)
{
    brightness = _brightness;
    if (matrix)
        matrix->setBrightness8(brightness);
}

#endif // USE_HUB75
