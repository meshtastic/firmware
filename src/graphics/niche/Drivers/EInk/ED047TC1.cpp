/*

    NicheGraphics parallel E-Ink driver for the LilyGo T5-S3-ePaper-Pro (ED047TC1).

    InkHUD buffer format : 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white
    FastEPD buffer format: 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white

    Both formats share the same pixel layout and polarity (1 = white, 0 = black).
    The InkHUD safe-area buffer (944×532) is copied into the centre of the physical
    960×540 FastEPD buffer so content clears the panel's inactive edge border.
    See ED047TC1.h for the H_OFFSET_BYTES / V_OFFSET_ROWS constants.

*/

// Ruler diagnostic — uncomment to draw calibration lines at each physical edge.
// #define EINK_EDGE_LINES

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./ED047TC1.h"

#include "FastEPD.h"
#include "configuration.h"

using namespace NicheGraphics::Drivers;

void ED047TC1::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    // Parallel display — SPI parameters are not used
    (void)spi;
    (void)pin_dc;
    (void)pin_cs;
    (void)pin_busy;
    (void)pin_rst;

    epaper = new FASTEPD;

#if defined(T5_S3_EPAPER_PRO_V1)
    epaper->initPanel(BB_PANEL_LILYGO_T5PRO, 28000000);
#elif defined(T5_S3_EPAPER_PRO_V2)
    epaper->initPanel(BB_PANEL_LILYGO_T5PRO_V2, 28000000);
    // Initialize all PCA9535 port-0 pins as outputs / HIGH
    for (int i = 0; i < 8; i++) {
        epaper->ioPinMode(i, OUTPUT);
        epaper->ioWrite(i, HIGH);
    }
#else
#error "ED047TC1 driver: unsupported variant — define T5_S3_EPAPER_PRO_V1 or T5_S3_EPAPER_PRO_V2"
#endif

    epaper->setMode(BB_MODE_1BPP);
    epaper->clearWhite();
    epaper->fullUpdate(true); // Blocking initial clear
}

void ED047TC1::update(uint8_t *imageData, UpdateTypes type)
{
    if (!epaper)
        return;

    // InkHUD renders into a DISPLAY_WIDTH × DISPLAY_HEIGHT safe-area buffer.
    // We need to place that into the centre of the physical 960×540 FastEPD buffer,
    // leaving blank margins at every edge to avoid the panel's inactive border.
    const uint32_t srcRowBytes = (DISPLAY_WIDTH + 7) / 8; // bytes per row in InkHUD buffer (118)
    const uint32_t dstRowBytes = (960 + 7) / 8;           // bytes per row in physical buffer (120)
    const uint32_t dstTotalRows = 540;

    uint8_t *cur = epaper->currentBuffer();

    // Fill physical buffer with white (0xFF = white in FastEPD 1bpp)
    memset(cur, 0xFF, dstRowBytes * dstTotalRows);

    // Copy each InkHUD row into the physical buffer with horizontal + vertical offsets
    for (uint32_t row = 0; row < DISPLAY_HEIGHT; row++) {
        const uint8_t *srcRow = imageData + row * srcRowBytes;
        uint8_t *dstRow = cur + (row + V_OFFSET_TOP) * dstRowBytes + H_OFFSET_BYTES;
        memcpy(dstRow, srcRow, srcRowBytes);
    }

#ifdef EINK_EDGE_LINES
    // Draw a 1px black box at the exact boundary of the safe area within the
    // physical buffer. If the margins are correct, all 4 lines should be
    // fully visible and right at the edge of the usable display area.

    auto setPixelBlack = [&](uint32_t col, uint32_t row) { cur[row * dstRowBytes + col / 8] &= ~(0x80 >> (col % 8)); };

    const uint32_t safeX = H_OFFSET_BYTES * 8;
    const uint32_t safeY = V_OFFSET_TOP;
    const uint32_t safeW = DISPLAY_WIDTH;
    const uint32_t safeH = DISPLAY_HEIGHT;

    // Top edge: horizontal line at safeY
    for (uint32_t col = safeX; col < safeX + safeW; col++)
        setPixelBlack(col, safeY);

    // Bottom edge: horizontal line at safeY + safeH - 1
    for (uint32_t col = safeX; col < safeX + safeW; col++)
        setPixelBlack(col, safeY + safeH - 1);

    // Left edge: vertical line at safeX
    for (uint32_t row = safeY; row < safeY + safeH; row++)
        setPixelBlack(safeX, row);

    // Right edge: vertical line at safeX + safeW - 1
    for (uint32_t row = safeY; row < safeY + safeH; row++)
        setPixelBlack(safeX + safeW - 1, row);
#endif

    if (type == FULL) {
        epaper->fullUpdate(CLEAR_SLOW, false);
    } else {
        // FAST update — InkHUD's setDisplayResilience() handles the FAST/FULL ratio
        epaper->fullUpdate(CLEAR_FAST, false);
    }

    epaper->backupPlane();

    // Signal InkHUD that the (blocking) update is complete
    beginPolling(1, 0);
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
