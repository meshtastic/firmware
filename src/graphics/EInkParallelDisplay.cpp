#include "EInkParallelDisplay.h"
#include "Wire.h"
#include "variant.h"
#include <Arduino.h>
#include <stdlib.h>

#if defined(USE_EPD)
#include "FastEPD.h"

// Thresholds for choosing partial vs full update
#ifndef EPD_PARTIAL_THRESHOLD_ROWS
#define EPD_PARTIAL_THRESHOLD_ROWS 64 // if changed region <= this many rows, prefer partial
#endif
#ifndef EPD_FULLSLOW_PERIOD
#define EPD_FULLSLOW_PERIOD 50 // every N full updates do a slow (CLEAR_SLOW) full refresh
#endif

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
        epaper->ioPinMode(0, OUTPUT);
        epaper->ioWrite(0, HIGH);
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
        const uint32_t base = (y >> 3) * w;               // (y/8) * width
        const uint8_t bitMask = (uint8_t)(1u << (y & 7)); // mask for this row in vertical-byte layout
        const uint32_t rowBase = y * rowBytes;

        // process full 8-pixel bytes
        for (uint32_t xb = 0; xb < rowBytes; ++xb) {
            uint32_t x0 = xb * 8;
            // read up to 8 source bytes (vertical-byte per column)
            uint8_t b0 = (x0 + 0 < w) ? buffer[base + x0 + 0] : 0;
            uint8_t b1 = (x0 + 1 < w) ? buffer[base + x0 + 1] : 0;
            uint8_t b2 = (x0 + 2 < w) ? buffer[base + x0 + 2] : 0;
            uint8_t b3 = (x0 + 3 < w) ? buffer[base + x0 + 3] : 0;
            uint8_t b4 = (x0 + 4 < w) ? buffer[base + x0 + 4] : 0;
            uint8_t b5 = (x0 + 5 < w) ? buffer[base + x0 + 5] : 0;
            uint8_t b6 = (x0 + 6 < w) ? buffer[base + x0 + 6] : 0;
            uint8_t b7 = (x0 + 7 < w) ? buffer[base + x0 + 7] : 0;

            // build output byte: MSB = leftmost pixel
            uint8_t out = 0;
            out |= (uint8_t)((b0 & bitMask) ? 0x80 : 0x00);
            out |= (uint8_t)((b1 & bitMask) ? 0x40 : 0x00);
            out |= (uint8_t)((b2 & bitMask) ? 0x20 : 0x00);
            out |= (uint8_t)((b3 & bitMask) ? 0x10 : 0x00);
            out |= (uint8_t)((b4 & bitMask) ? 0x08 : 0x00);
            out |= (uint8_t)((b5 & bitMask) ? 0x04 : 0x00);
            out |= (uint8_t)((b6 & bitMask) ? 0x02 : 0x00);
            out |= (uint8_t)((b7 & bitMask) ? 0x01 : 0x00);

            // handle partial byte at end of row by masking off invalid bits
            uint8_t mask = 0xFF;
            uint32_t bitsRemain = (w > x0) ? (w - x0) : 0;
            if (bitsRemain > 0 && bitsRemain < 8) {
                mask = (uint8_t)(0xFF << (8 - bitsRemain));
                out &= mask;
            }

            // invert to FASTEPD polarity
            out = (~out) & mask;

            uint32_t pos = rowBase + xb;
            uint8_t prevVal = prev ? (prev[pos] & mask) : 0x00;
            if (prev && prevVal == out) {
                // unchanged
                continue;
            }

            // mark row changed
            if (y < (uint32_t)newTop)
                newTop = y;
            if ((int)y > newBottom)
                newBottom = y;

            // write to current buffer preserving masked bits
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
        epaper->fullUpdate(CLEAR_SLOW, false);
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
    epaper->fullUpdate(CLEAR_FAST, false);
    epaper->backupPlane();
}

#endif