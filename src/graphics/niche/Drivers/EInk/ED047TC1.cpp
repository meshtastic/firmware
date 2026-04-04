/*

    NicheGraphics parallel E-Ink driver for the LilyGo T5-S3-ePaper-Pro (ED047TC1).

    InkHUD buffer format : 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white
    FastEPD buffer format: 1bpp, horizontal bytes, MSB = leftmost pixel, 1 = white

    Both formats share the same pixel layout and polarity (1 = white, 0 = black),
    so a direct copy is all that is needed.

*/

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

    const uint32_t rowBytes = (width + 7) / 8;
    const uint32_t bufSize = rowBytes * height;
    uint8_t *cur = epaper->currentBuffer();

    // Direct copy: InkHUD and FastEPD both use 1=white, 0=black
    for (uint32_t i = 0; i < bufSize; i++)
        cur[i] = imageData[i];

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
