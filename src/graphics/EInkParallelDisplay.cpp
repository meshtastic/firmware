#include "EInkParallelDisplay.h"
#include "Wire.h"
#include "variant.h"
#include <Arduino.h>
#include <stdlib.h>

#if defined(USE_EPD)
#include "FastEPD.h"

EInkParallelDisplay::EInkParallelDisplay(uint16_t width, uint16_t height, EpdRotation rotation) : epaper(nullptr)
{
    LOG_INFO("ctor EInkParallelDisplay");
    // Set dimensions in OLEDDisplay base class
    this->geometry = GEOMETRY_RAWMODE;
    this->displayWidth = EPD_WIDTH;
    this->displayHeight = EPD_HEIGHT;

    // Round shortest side up to nearest byte, to prevent truncation causing an undersized buffer
    uint16_t shortSide = min(EPD_WIDTH, EPD_HEIGHT);
    uint16_t longSide = max(EPD_WIDTH, EPD_HEIGHT);
    if (shortSide % 8 != 0)
        shortSide = (shortSide | 7) + 1;

    this->displayBufferSize = longSide * (shortSide / 8);
}

EInkParallelDisplay::~EInkParallelDisplay()
{
    delete epaper;
}

/*
 * Called by the OLEDDisplay::init() path.
 */
bool EInkParallelDisplay::connect()
{
    LOG_INFO("Do EPD init");
    if (!epaper) {
        epaper = new FASTEPD;
#if defined(T5_S3_EPAPER_PRO_V1)
        epaper->initPanel(BB_PANEL_LILYGO_T5PRO, 28000000);
#elif defined(T5_S3_EPAPER_PRO_V2)
        epaper->initPanel(BB_PANEL_LILYGO_T5PRO_V2, 28000000);
#else
#error "unsupported EPD device!"
#endif
    }

    epaper->setMode(BB_MODE_1BPP);
    epaper->clearWhite();
    epaper->fullUpdate(true);

    return true;
}

/*
 * sendCommand - simple passthrough (not required for epd_driver-based path)
 */
void EInkParallelDisplay::sendCommand(uint8_t com)
{
    LOG_DEBUG("EInkParallelDisplay::sendCommand %d", (int)com);
}

/*
 * Convert the OLEDDisplay buffer (vertical byte layout) into the 1bpp horizontal-bytes
 * buffer used by the FASTEPD library. For performance we write directly into FASTEPD's
 * currentBuffer() while comparing against previousBuffer() to detect changed rows.
 * After conversion we call FASTEPD::partialUpdate() or FASTEPD::fullUpdate() according
 * to a heuristic so only the minimal region is refreshed.
 */
void EInkParallelDisplay::display(void)
{
    LOG_DEBUG("EInkParallelDisplay::display");

    const uint16_t w = this->displayWidth;
    const uint16_t h = this->displayHeight;
    static int iUpdates = 0; // count eink updates to know when to do a fullUpdate()

    // bytes per row in epd format (one byte = 8 horizontal pixels)
    const uint32_t rowBytes = (w + 7) / 8;

    // Get pointers to internal buffers
    uint8_t *cur = epaper->currentBuffer();
    uint8_t *prev = epaper->previousBuffer(); // may be NULL on first init

    // Track changed row range while converting
    int newTop = h;     // min changed row (initialized to out-of-range)
    int newBottom = -1; // max changed row

    // Convert: OLED buffer layout -> FASTEPD 1bpp horizontal-bytes layout into cur,
    // comparing against prev when available to detect changes.
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t rowBase = y * rowBytes;
        for (uint32_t xb = 0; xb < rowBytes; ++xb) {
            uint8_t out = 0;
            for (uint8_t bit = 0; bit < 8; ++bit) {
                uint32_t x = xb * 8 + bit;
                uint8_t pix = 0;
                if (x < w) {
                    uint32_t idx = x + (y / 8) * w;
                    pix = (buffer[idx] >> (y & 7)) & 1;
                }
                // FASTEPD expects MSB = leftmost pixel
                out |= (pix & 1) << (7 - bit);
            }

            // If this is a partial byte at the row end, build a mask for valid bits
            uint8_t mask = 0xFF;
            uint32_t bitsRemain = w - xb * 8;
            if (bitsRemain < 8) {
                mask = (uint8_t)(0xFF << (8 - bitsRemain));
                out &= mask;
            }

            // Invert bits to match FASTEPD color convention (panel uses opposite polarity)
            out = (~out) & mask;

            uint32_t pos = rowBase + xb;
            uint8_t prevVal = prev ? (prev[pos] & mask) : 0x00; // if no prev, force change
            if (prev && prevVal == out) {
                // no change for these bits; keep cur as-is (do not overwrite)
                continue;
            }

            // mark row y as changed
            if (y < (uint32_t)newTop)
                newTop = y;
            if ((int)y > newBottom)
                newBottom = y;

            // write new value into current buffer preserving any masked bits
            cur[pos] = (cur[pos] & ~mask) | out;
        }
    }

    // If nothing changed, avoid any panel update
    if (newBottom < 0) {
        LOG_DEBUG("no pixel changes detected, skipping update");
        return;
    }

    // Choose partial vs full update using heuristic
    if (epaper->getMode() == BB_MODE_1BPP && iUpdates < 50) {
        epaper->partialUpdate(true, newTop, newBottom);
    } else {
        epaper->fullUpdate(CLEAR_NONE, false);
        iUpdates = 0;
    }
    iUpdates++;

    lastDrawMsec = millis();
}

/*
 * forceDisplay: use lastDrawMsec
 */
bool EInkParallelDisplay::forceDisplay(uint32_t msecLimit)
{
    uint32_t now = millis();
    if (lastDrawMsec == 0 || (now - lastDrawMsec) > msecLimit) {
        display();
        return true;
    }
    return false;
}

void EInkParallelDisplay::endUpdate()
{
    epaper->backupPlane();
}

#endif