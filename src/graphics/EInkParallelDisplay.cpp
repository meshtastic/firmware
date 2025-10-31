#include "EInkParallelDisplay.h"

#ifdef USE_EPD

#include "SPILock.h"
#include "Wire.h"
#include "variant.h"
#include <Arduino.h>
#include <atomic>
#include <stdlib.h>
#include <string.h>

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

#ifdef EINK_LIMIT_GHOSTING_PX
    // allocate dirty pixel buffer same size as epaper buffers (rowBytes * height)
    size_t rowBytes = (this->displayWidth + 7) / 8;
    dirtyPixelsSize = rowBytes * this->displayHeight;
    dirtyPixels = (uint8_t *)calloc(dirtyPixelsSize, 1);
    ghostPixelCount = 0;
#endif
}

EInkParallelDisplay::~EInkParallelDisplay()
{
#ifdef EINK_LIMIT_GHOSTING_PX
    if (dirtyPixels) {
        free(dirtyPixels);
        dirtyPixels = nullptr;
    }
#endif
    // If an async full update is running, wait for it to finish
    if (asyncFullRunning.load()) {
        // wait a short while for task to finish
        for (int i = 0; i < 50 && asyncFullRunning.load(); ++i) {
            delay(50);
        }
        if (asyncTaskHandle) {
            // Let it finish or delete it
            vTaskDelete(asyncTaskHandle);
            asyncTaskHandle = nullptr;
        }
    }

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

#ifdef EINK_LIMIT_GHOSTING_PX
    // After a full/clear the dirty tracking should be reset
    resetGhostPixelTracking();
#endif

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
 * Start a background task that will perform a blocking fullUpdate(). This lets
 * display() return quickly while the heavy refresh runs in the background.
 */
void EInkParallelDisplay::startAsyncFullUpdate(int clearMode)
{
    if (asyncFullRunning.load())
        return; // already running

    asyncFullRunning.store(true);
    // pass 'this' as parameter
    BaseType_t rc = xTaskCreatePinnedToCore(EInkParallelDisplay::asyncFullUpdateTask, "epd_full", 4096 / sizeof(StackType_t),
                                            this, 2, &asyncTaskHandle,
#if CONFIG_FREERTOS_UNICORE
                                            0
#else
                                            1
#endif
    );
    if (rc != pdPASS) {
        LOG_WARN("Failed to create async full-update task, falling back to blocking update");
        // fallback: blocking call
        {
            concurrency::LockGuard g(spiLock);
            epaper->fullUpdate(clearMode, false);
            epaper->backupPlane();
        }
        asyncFullRunning.store(false);
        asyncTaskHandle = nullptr;
    }
}

/*
 * FreeRTOS task entry: runs the full update and then backs up plane.
 */
void EInkParallelDisplay::asyncFullUpdateTask(void *pvParameters)
{
    EInkParallelDisplay *self = static_cast<EInkParallelDisplay *>(pvParameters);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }

    // Acquire SPI lock and run the full update inside the critical section
    {
        concurrency::LockGuard g(spiLock);
        // choose CLEAR_SLOW occasionally
        int clearMode = CLEAR_FAST;
        if (self->fastRefreshCount >= EPD_FULLSLOW_PERIOD) {
            clearMode = CLEAR_SLOW;
            self->fastRefreshCount = 0;
        } else {
            // when running async full, treat it as a full so reset fast count
            self->fastRefreshCount = 0;
        }

        self->epaper->fullUpdate(clearMode, false);
        self->epaper->backupPlane();
    }

#ifdef EINK_LIMIT_GHOSTING_PX
    // A full refresh clears ghosting state
    self->resetGhostPixelTracking();
#endif

    self->asyncFullRunning.store(false);
    self->asyncTaskHandle = nullptr;

    // delete this task
    vTaskDelete(nullptr);
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

    // Simple rate limiting: avoid very-frequent responsive updates
    uint32_t nowMs = millis();
    if (lastUpdateMs != 0 && (nowMs - lastUpdateMs) < RESPONSIVE_MIN_MS) {
        LOG_DEBUG("rate-limited, skipping update");
        return;
    }

    // bytes per row in epd format (one byte = 8 horizontal pixels)
    const uint32_t rowBytes = (w + 7) / 8;

    // Get pointers to internal buffers
    uint8_t *cur = epaper->currentBuffer();
    uint8_t *prev = epaper->previousBuffer(); // may be NULL on first init

    // Track changed row range while converting
    int newTop = h;     // min changed row (initialized to out-of-range)
    int newBottom = -1; // max changed row

    // Compute a quick hash of the incoming OLED buffer (so we can skip identical frames)
    uint32_t imageHash = 0;
    uint32_t bufBytes = (w / 8) * h; // vertical-byte layout size
    for (uint32_t bi = 0; bi < bufBytes; ++bi) {
        imageHash ^= ((uint32_t)buffer[bi]) << (bi & 31);
    }
    if (imageHash == previousImageHash) {
        LOG_DEBUG("image identical to previous, skipping update");
        return;
    }

#ifdef EINK_LIMIT_GHOSTING_PX
    // reset ghost count for this conversion pass; we'll mark bits that change
    ghostPixelCount = 0;
#endif

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

            // If ghost-pixel tracking is enabled, mark bits that will change
#ifdef EINK_LIMIT_GHOSTING_PX
            markDirtyBits(prev, pos, mask, out);
#endif

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
        LOG_DEBUG("no pixel changes detected, skipping update (conv)");
        previousImageHash = imageHash; // still remember that frame
        return;
    }

    // Choose partial vs full update using heuristic
    concurrency::LockGuard g(spiLock);

    // Decide if we should force a full update after many fast updates
    bool forceFull = (fastRefreshCount >= EPD_FULLSLOW_PERIOD);

#ifdef EINK_LIMIT_GHOSTING_PX
    // If ghost pixels exceed limit, force a full update to clear ghosting
    if (ghostPixelCount > ghostPixelLimit) {
        LOG_WARN("ghost pixels %u > limit %u, forcing full refresh", ghostPixelCount, ghostPixelLimit);
        forceFull = true;
    }
#endif

    if (epaper->getMode() == BB_MODE_1BPP && !forceFull && (newBottom - newTop) <= EPD_PARTIAL_THRESHOLD_ROWS) {
        epaper->partialUpdate(true, newTop, newBottom);
        fastRefreshCount++;
    } else {
        // Full update: prefer to run asynchronously so UI thread isn't blocked.
        // If async running isn't available/fails, startAsyncFullUpdate() falls back to blocking call.
        startAsyncFullUpdate(forceFull ? CLEAR_SLOW : CLEAR_FAST);
    }
    iUpdates++;

    lastUpdateMs = millis();
    previousImageHash = imageHash;

    // Keep same behavior as before
    lastDrawMsec = millis();
}

#ifdef EINK_LIMIT_GHOSTING_PX
// markDirtyBits: mark per-bit dirty flags and update ghostPixelCount
void EInkParallelDisplay::markDirtyBits(const uint8_t *prevBuf, uint32_t pos, uint8_t mask, uint8_t out)
{
    // prevVal is previous displayed bits for this byte (masked)
    uint8_t prevVal = prevBuf[pos] & mask;

    // before = dirty bits previously recorded for this byte
    uint8_t before = dirtyPixels[pos];

    // In this code 'out' uses FASTEPD polarity (1 = black pixel, 0 = white)
    uint8_t blackBits = out & mask;    // bits that will be black in the new image
    uint8_t whiteBits = (~out) & mask; // bits that will be white in the new image

    // Ghost bits: locations that were marked dirty previously and now will be white
    uint8_t ghostBits = before & whiteBits;
    if (ghostBits) {
        ghostPixelCount += __builtin_popcount((unsigned)ghostBits);
    }

    // Update dirty bitmap: mark locations that will be black now
    uint8_t newlySet = blackBits & (~before);
    if (newlySet) {
        dirtyPixels[pos] |= newlySet;
    }

    // Note: we do NOT clear dirty bits here when a pixel goes white; they remain
    // cleared only on a full refresh (resetGhostPixelTracking()).
}

// reset ghost tracking (call after a full refresh)
void EInkParallelDisplay::resetGhostPixelTracking()
{
    if (!dirtyPixels)
        return;
    memset(dirtyPixels, 0, dirtyPixelsSize);
    ghostPixelCount = 0;
}
#endif

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
    {
        concurrency::LockGuard g(spiLock);
        // ensure any async full update is started/completed
        if (asyncFullRunning.load()) {
            // nothing to do; background task will run and call backupPlane when done
        } else {
            epaper->fullUpdate(CLEAR_FAST, false);
            epaper->backupPlane();
#ifdef EINK_LIMIT_GHOSTING_PX
            resetGhostPixelTracking();
#endif
        }
    }
    epaper->backupPlane();
}

#endif