#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Renderer.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
#include "./Applets/User/BookReader/BookReaderApplet.h"
#include "PowerFSM.h"
#endif
#include "main.h"
#if INKHUD_BOOKREADER_GRAYSCALE
#include <array>
#include <esp_heap_caps.h>
#endif

#include "./Applet.h"
#include "./SystemApplet.h"
#include "./Tile.h"

using namespace NicheGraphics;

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
#ifndef INKHUD_BOOKREADER_PERF_LOG
#define INKHUD_BOOKREADER_PERF_LOG 0
#endif

#if INKHUD_BOOKREADER_PERF_LOG
static uint32_t readerRefreshStartMs = 0;
#endif
#endif

InkHUD::Renderer::Renderer() : concurrency::OSThread("Renderer")
{
    // Nothing for the timer to do just yet
    OSThread::disable();

    // Convenient references
    inkhud = InkHUD::getInstance();
    settings = &inkhud->persistence->settings;
}

// Connect the (fully set-up) E-Ink driver to InkHUD
// Should happen in your variant's nicheGraphics.h file, before InkHUD::begin is
// called
void InkHUD::Renderer::setDriver(Drivers::EInk *driver)
{
    // Make sure not already set
    if (this->driver) {
        LOG_ERROR("Driver already set");
        delay(2000); // Wait for native serial..
        assert(false);
    }

    // Store the driver which was created in setupNicheGraphics()
    this->driver = driver;

    // Determine the dimensions of the image buffer, in bytes.
    // Along rows, pixels are stored 8 per byte.
    // Not all display widths are divisible by 8. Need to make sure bytecount
    // accommodates padding for these.
    imageBufferWidth = ((driver->width - 1) / 8) + 1;
    imageBufferHeight = driver->height;

    // Allocate the image buffer
    imageBuffer = new uint8_t[imageBufferWidth * imageBufferHeight];
}

// Set the target number of FAST display updates in a row, before a FULL update
// is used for display health This value applies only to updates with an
// UNSPECIFIED update type If explicitly requested FAST updates exceed this
// target, the stressMultiplier parameter determines how many subsequent FULL
// updates will be performed, in an attempt to restore the display's health
void InkHUD::Renderer::setDisplayResilience(uint8_t fastPerFull, float stressMultiplier)
{
    displayHealth.fastPerFull = fastPerFull;
    displayHealth.stressMultiplier = stressMultiplier;
}

void InkHUD::Renderer::begin()
{
    forceUpdate(Drivers::EInk::UpdateTypes::FULL, true, false);
}

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
#if INKHUD_BOOKREADER_GRAYSCALE
void InkHUD::Renderer::setGrayPixel(uint16_t x, uint16_t y, uint8_t luminance)
{
    uint8_t &byte = grayBuffer[static_cast<size_t>(y) * ((driver->width + 1U) / 2U) + x / 2U];
    const unsigned shift = (x & 1U) ? 0U : 4U;
    const uint8_t previous = (byte >> shift) & 15U;
    grayPixelCount -= previous != 0U && previous != 15U;
    grayPixelCount += luminance != 0U && luminance != 15U;
    byte = (byte & ~(15U << shift)) | (luminance << shift);
}

#if defined(ARCH_ESP32) && defined(__GNUC__)
// Favor speed for the packed-pixel blit without changing panel timing.
__attribute__((optimize("O3")))
#endif
void InkHUD::Renderer::writeGrayBitmap(int16_t screenX, int16_t screenY, uint16_t w, uint16_t h, const uint8_t *bitmap,
                                       uint16_t bitmapStride, uint16_t sourceX, uint16_t sourceY)
{
    if (!bitmap || !w || !h)
        return;
    if (!grayBuffer && driver->supportsGrayscale()) {
        grayBuffer = static_cast<uint8_t *>(
            heap_caps_malloc(((driver->width + 1U) / 2U) * driver->height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (grayBuffer) {
            memset(grayBuffer, 0xFF, ((driver->width + 1U) / 2U) * driver->height);
            for (uint16_t y = 0; y < driver->height; ++y)
                for (uint16_t x = 0; x < driver->width; ++x)
                    setGrayPixel(x, y, (imageBuffer[y * imageBufferWidth + x / 8U] & (0x80U >> (x & 7U))) ? 15U : 0U);
        }
    }
    constexpr uint8_t bayer[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const int32_t firstX = std::max<int32_t>(0, -static_cast<int32_t>(screenX));
    const int32_t firstY = std::max<int32_t>(0, -static_cast<int32_t>(screenY));
    const int32_t endX = std::min<int32_t>(w, static_cast<int32_t>(width()) - screenX);
    const int32_t endY = std::min<int32_t>(h, static_cast<int32_t>(height()) - screenY);
    if (firstX >= endX || firstY >= endY)
        return;
    const uint16_t monoStride = imageBufferWidth;
    const uint16_t grayStride = (driver->width + 1U) / 2U;
    uint8_t *const mono = imageBuffer;
    uint8_t *const gray = grayBuffer;
    uint32_t intermediatePixels = grayPixelCount;
    static constexpr auto intermediateCount = [] {
        std::array<uint8_t, 256> counts{};
        for (unsigned pair = 0; pair < counts.size(); ++pair)
            counts[pair] = ((pair >> 4U) != 0U && (pair >> 4U) != 15U) + ((pair & 15U) != 0U && (pair & 15U) != 15U);
        return counts;
    }();
#if INKHUD_BOOKREADER_PERF_LOG
    const uint32_t blitStartMs = millis();
    uint32_t inkHistogram[16] = {};
    uint32_t blitHash = 2166136261U;
#endif
    const auto writePixel = [&](uint8_t *monoRow, uint8_t *grayRow, int16_t x, int32_t sx, int32_t sy, uint8_t ink) {
#if INKHUD_BOOKREADER_PERF_LOG
        ++inkHistogram[ink];
        blitHash = (blitHash ^ ink) * 16777619U;
#endif
        if (grayRow) {
            uint8_t &byte = grayRow[x / 2U];
            const unsigned shift = (x & 1U) ? 0U : 4U;
            const uint8_t previous = (byte >> shift) & 15U;
            intermediatePixels -= previous != 0U && previous != 15U;
            intermediatePixels += ink != 0U && ink != 15U;
            byte = (byte & ~(15U << shift)) | ((15U - ink) << shift);
        } else {
            const bool black = ink * 32U > (bayer[((screenY + sy) & 3U) * 4U + ((screenX + sx) & 3U)] * 2U + 1U) * 15U;
            bitWrite(monoRow[x / 8U], 7U - (x & 7U), !black);
        }
    };
    const auto writeGraySpan = [&](uint8_t *row, int16_t x, int step, const uint8_t *source, int count, int sourceStride) {
        int i = 0;
        if ((x & 1U) != (step < 0)) {
            writePixel(nullptr, row, x, 0, 0, source[0]);
            x += step;
            ++i;
        }
        for (; i + 1 < count; i += 2, x += 2 * step) {
            const uint8_t first = source[i * sourceStride];
            const uint8_t second = source[(i + 1) * sourceStride];
#if INKHUD_BOOKREADER_PERF_LOG
            ++inkHistogram[first];
            ++inkHistogram[second];
            blitHash = ((blitHash ^ first) * 16777619U ^ second) * 16777619U;
#endif
            uint8_t &byte = row[x / 2U];
            const uint8_t packedInk = step > 0 ? (first << 4U) | second : (second << 4U) | first;
            intermediatePixels -= intermediateCount[byte];
            intermediatePixels += intermediateCount[packedInk];
            byte = packedInk ^ 0xFFU;
        }
        if (i < count)
            writePixel(nullptr, row, x, 0, 0, source[i * sourceStride]);
    };
    // Transpose small SRAM tiles so rotated output does not thrash the PSRAM cache.
    constexpr int32_t tileSize = 16;
    uint8_t inks[tileSize][tileSize];
    const uint8_t rotation = settings->rotation;
    for (int32_t ty = firstY; ty < endY; ty += tileSize) {
        const int32_t tileH = std::min(tileSize, endY - ty);
        for (int32_t tx = firstX; tx < endX; tx += tileSize) {
            const int32_t tileW = std::min(tileSize, endX - tx);
            for (int32_t sy = 0; sy < tileH; ++sy) {
                const uint8_t *srcRow = bitmap + static_cast<size_t>(sourceY + ty + sy) * bitmapStride;
                uint16_t srcX = sourceX + tx;
                int32_t sx = 0;
                if (srcX & 1U) {
                    inks[sy][sx++] = srcRow[srcX++ / 2U] & 15U;
                }
                for (; sx + 1 < tileW; sx += 2, srcX += 2) {
                    const uint8_t pair = srcRow[srcX / 2U];
                    inks[sy][sx] = pair >> 4U;
                    inks[sy][sx + 1] = pair & 15U;
                }
                if (sx < tileW)
                    inks[sy][sx] = srcRow[srcX / 2U] >> 4U;
            }
            if (rotation & 1U) {
                for (int32_t sx = 0; sx < tileW; ++sx) {
                    int16_t x = screenX + tx + sx;
                    int16_t y = screenY + ty;
                    rotatePixelCoords(&x, &y);
                    uint8_t *const monoRow = mono + static_cast<size_t>(y) * monoStride;
                    uint8_t *const grayRow = gray ? gray + static_cast<size_t>(y) * grayStride : nullptr;
                    if (grayRow) {
                        writeGraySpan(grayRow, x, rotation == 1 ? -1 : 1, reinterpret_cast<const uint8_t *>(inks) + sx, tileH,
                                      tileSize);
                        continue;
                    }
                    for (int32_t sy = 0; sy < tileH; ++sy) {
                        writePixel(monoRow, grayRow, x, tx + sx, ty + sy, inks[sy][sx]);
                        x += rotation == 1 ? -1 : 1;
                    }
                }
            } else {
                for (int32_t sy = 0; sy < tileH; ++sy) {
                    int16_t x = screenX + tx;
                    int16_t y = screenY + ty + sy;
                    rotatePixelCoords(&x, &y);
                    uint8_t *const monoRow = mono + static_cast<size_t>(y) * monoStride;
                    uint8_t *const grayRow = gray ? gray + static_cast<size_t>(y) * grayStride : nullptr;
                    if (grayRow) {
                        writeGraySpan(grayRow, x, rotation == 0 ? 1 : -1, reinterpret_cast<const uint8_t *>(inks) + sy * tileSize,
                                      tileW, 1);
                        continue;
                    }
                    for (int32_t sx = 0; sx < tileW; ++sx) {
                        writePixel(monoRow, grayRow, x, tx + sx, ty + sy, inks[sy][sx]);
                        x += rotation == 0 ? 1 : -1;
                    }
                }
            }
        }
    }
    grayPixelCount = intermediatePixels;
    if (gray)
        monoBufferNeedsSync = true;
#if INKHUD_BOOKREADER_PERF_LOG
    LOG_INFO("[BookReaderDraw] blit xy=%d,%d size=%ux%u src=%u,%u "
             "clip=%ld,%ld-%ld,%ld rotation=%u gray=%u hash=%08lx ms=%lu",
             screenX, screenY, w, h, sourceX, sourceY, static_cast<long>(firstX), static_cast<long>(firstY),
             static_cast<long>(endX), static_cast<long>(endY), rotation, gray != nullptr, static_cast<unsigned long>(blitHash),
             static_cast<unsigned long>(millis() - blitStartMs));
    LOG_INFO("[BookReaderDraw] image ink bins "
             "white-to-black=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
             "%lu,%lu,%lu",
             inkHistogram[0], inkHistogram[1], inkHistogram[2], inkHistogram[3], inkHistogram[4], inkHistogram[5],
             inkHistogram[6], inkHistogram[7], inkHistogram[8], inkHistogram[9], inkHistogram[10], inkHistogram[11],
             inkHistogram[12], inkHistogram[13], inkHistogram[14], inkHistogram[15]);
#endif
}
#endif

// Fast path for blitting MSB-first 1-bpp bitmaps into imageBuffer.
void InkHUD::Renderer::writeBitmap(int16_t screenX, int16_t screenY, uint16_t w, uint16_t h, const uint8_t *bitmap,
                                   uint16_t bitmapStride, uint16_t sourceX, uint16_t sourceY)
{
    if (!bitmap || w == 0 || h == 0) {
        return;
    }
#if INKHUD_BOOKREADER_GRAYSCALE
    if (grayBuffer) {
        for (uint16_t y = 0; y < h; ++y)
            for (uint16_t x = 0; x < w; ++x) {
                const uint16_t sx = sourceX + x;
                const bool black = bitmap[(sourceY + y) * bitmapStride + sx / 8U] & (0x80U >> (sx & 7U));
                if (screenX + x >= 0 && screenY + y >= 0 && screenX + x < width() && screenY + y < height())
                    handlePixel(screenX + x, screenY + y, black ? BLACK : WHITE);
            }
        return;
    }
#endif

    switch (settings->rotation) {
    case 0: {
        // No rotation: iterate row-first for sequential imageBuffer access.
        for (uint16_t sy = 0; sy < h; sy++) {
            const int16_t bufY = static_cast<int16_t>(screenY + sy);
            if (bufY < 0 || static_cast<uint16_t>(bufY) >= imageBufferHeight) {
                continue;
            }
            const uint8_t *srcRow = bitmap + static_cast<size_t>(sourceY + sy) * bitmapStride;
            uint8_t *dstRow = imageBuffer + static_cast<size_t>(bufY) * imageBufferWidth;
            for (uint16_t sx = 0; sx < w; sx++) {
                const int16_t bufX = static_cast<int16_t>(screenX + sx);
                if (bufX < 0 || static_cast<uint16_t>(bufX) >= driver->width) {
                    continue;
                }
                const uint16_t srcX = static_cast<uint16_t>(sourceX + sx);
                const bool black = (srcRow[srcX >> 3] & (static_cast<uint8_t>(0x80U) >> (srcX & 7U))) != 0U;
                const uint8_t bitMask = static_cast<uint8_t>(0x80U) >> (static_cast<uint16_t>(bufX) & 7U);
                if (black) {
                    dstRow[static_cast<uint16_t>(bufX) >> 3] &= static_cast<uint8_t>(~bitMask);
                } else {
                    dstRow[static_cast<uint16_t>(bufX) >> 3] |= bitMask;
                }
            }
        }
        break;
    }
    case 1: {
        // 90 degrees CW: iterate column-first so each inner loop writes one
        // physical row.
        const uint16_t dW = driver->width;
        for (uint16_t sx = 0; sx < w; sx++) {
            const int16_t physRow = static_cast<int16_t>(screenX + sx);
            if (physRow < 0 || static_cast<uint16_t>(physRow) >= imageBufferHeight) {
                continue;
            }
            uint8_t *dstRow = imageBuffer + static_cast<size_t>(physRow) * imageBufferWidth;
            const uint16_t srcX = static_cast<uint16_t>(sourceX + sx);
            const uint8_t srcMask = static_cast<uint8_t>(0x80U) >> (srcX & 7U);
            const uint16_t srcByteOff = srcX >> 3;
            for (uint16_t sy = 0; sy < h; sy++) {
                const int32_t physColI =
                    static_cast<int32_t>(dW - 1U) - (static_cast<int32_t>(screenY) + static_cast<int32_t>(sy));
                if (physColI < 0 || static_cast<uint32_t>(physColI) >= driver->width) {
                    continue;
                }
                const uint16_t physCol = static_cast<uint16_t>(physColI);
                const bool black = (bitmap[static_cast<size_t>(sourceY + sy) * bitmapStride + srcByteOff] & srcMask) != 0U;
                const uint8_t bitMask = static_cast<uint8_t>(0x80U) >> (physCol & 7U);
                if (black) {
                    dstRow[physCol >> 3] &= static_cast<uint8_t>(~bitMask);
                } else {
                    dstRow[physCol >> 3] |= bitMask;
                }
            }
        }
        break;
    }
    case 2: {
        // 180 degrees: iterate row-first for sequential physical row access.
        const uint32_t physicalWidth = driver->width;
        for (uint16_t sy = 0; sy < h; sy++) {
            const int32_t physRowI =
                static_cast<int32_t>(imageBufferHeight - 1U) - (static_cast<int32_t>(screenY) + static_cast<int32_t>(sy));
            if (physRowI < 0 || static_cast<uint32_t>(physRowI) >= static_cast<uint32_t>(imageBufferHeight)) {
                continue;
            }
            uint8_t *dstRow = imageBuffer + static_cast<size_t>(physRowI) * imageBufferWidth;
            const uint8_t *srcRow = bitmap + static_cast<size_t>(sourceY + sy) * bitmapStride;
            for (uint16_t sx = 0; sx < w; sx++) {
                const int32_t physColI =
                    static_cast<int32_t>(physicalWidth - 1U) - (static_cast<int32_t>(screenX) + static_cast<int32_t>(sx));
                if (physColI < 0 || static_cast<uint32_t>(physColI) >= physicalWidth) {
                    continue;
                }
                const uint16_t physCol = static_cast<uint16_t>(physColI);
                const uint16_t srcX = static_cast<uint16_t>(sourceX + sx);
                const bool black = (srcRow[srcX >> 3] & (static_cast<uint8_t>(0x80U) >> (srcX & 7U))) != 0U;
                const uint8_t bitMask = static_cast<uint8_t>(0x80U) >> (physCol & 7U);
                if (black) {
                    dstRow[physCol >> 3] &= static_cast<uint8_t>(~bitMask);
                } else {
                    dstRow[physCol >> 3] |= bitMask;
                }
            }
        }
        break;
    }
    case 3: {
        // 270 degrees CW: iterate column-first so each inner loop writes one
        // physical row.
        for (uint16_t sx = 0; sx < w; sx++) {
            const int32_t physRowI =
                static_cast<int32_t>(imageBufferHeight - 1U) - (static_cast<int32_t>(screenX) + static_cast<int32_t>(sx));
            if (physRowI < 0 || static_cast<uint32_t>(physRowI) >= static_cast<uint32_t>(imageBufferHeight)) {
                continue;
            }
            const uint16_t physRow = static_cast<uint16_t>(physRowI);
            uint8_t *dstRow = imageBuffer + static_cast<size_t>(physRow) * imageBufferWidth;
            const uint16_t srcX = static_cast<uint16_t>(sourceX + sx);
            const uint8_t srcMask = static_cast<uint8_t>(0x80U) >> (srcX & 7U);
            const uint16_t srcByteOff = srcX >> 3;
            for (uint16_t sy = 0; sy < h;) {
                const int16_t physCol = static_cast<int16_t>(screenY + sy);
                if (physCol < 0 || static_cast<uint16_t>(physCol) >= driver->width) {
                    ++sy;
                    continue;
                }
                // Transpose eight source rows into one destination byte without
                // changing any pixels.
                if ((physCol & 7) == 0 && static_cast<uint32_t>(sy) + 8U <= h &&
                    static_cast<uint32_t>(physCol) + 8U <= driver->width) {
                    uint8_t blackBits = 0;
                    for (uint16_t row = 0; row < 8U; ++row) {
                        if (bitmap[static_cast<size_t>(sourceY + sy + row) * bitmapStride + srcByteOff] & srcMask)
                            blackBits |= static_cast<uint8_t>(0x80U >> row);
                    }
                    dstRow[static_cast<uint16_t>(physCol) >> 3] = static_cast<uint8_t>(~blackBits);
                    sy += 8U;
                    continue;
                }
                const bool black = (bitmap[static_cast<size_t>(sourceY + sy) * bitmapStride + srcByteOff] & srcMask) != 0U;
                const uint8_t bitMask = static_cast<uint8_t>(0x80U) >> (static_cast<uint16_t>(physCol) & 7U);
                if (black) {
                    dstRow[physCol >> 3] &= static_cast<uint8_t>(~bitMask);
                } else {
                    dstRow[physCol >> 3] |= bitMask;
                }
                ++sy;
            }
        }
        break;
    }
    default: {
        // Unexpected rotation: fall back to per-pixel handlePixel.
        for (uint16_t sy = 0; sy < h; sy++) {
            const uint8_t *srcRow = bitmap + static_cast<size_t>(sourceY + sy) * bitmapStride;
            for (uint16_t sx = 0; sx < w; sx++) {
                const uint16_t srcX = static_cast<uint16_t>(sourceX + sx);
                const bool black = (srcRow[srcX >> 3] & (static_cast<uint8_t>(0x80U) >> (srcX & 7U))) != 0U;
                handlePixel(static_cast<int16_t>(screenX + sx), static_cast<int16_t>(screenY + sy), black ? BLACK : WHITE);
            }
        }
        break;
    }
    }
}

#endif

// Set a flag, which will be picked up by runOnce, ASAP.
// Quite likely, multiple applets will all want to respond to one event
// (Observable, etc) Each affected applet can independently call
// requestUpdate(), and all share the one opportunity to render, at next runOnce
void InkHUD::Renderer::requestUpdate(bool all)
{
    requested = true;
    renderAll |= all;

    // We will run the thread as soon as we loop(),
    // after all Applets have had a chance to observe whatever event set this off
    OSThread::setIntervalFromNow(0);
    OSThread::enabled = true;
    runASAP = true;
}

// requestUpdate will not actually update if no requests were made by applets
// which are actually visible This can occur, because applets requestUpdate even
// from the background, in case the user's autoshow settings permit them to be
// moved to foreground. Sometimes, however, we will want to trigger a display
// update manually, in the absence of any sort of applet event Display health,
// for example. In these situations, we use forceUpdate
void InkHUD::Renderer::forceUpdate(Drivers::EInk::UpdateTypes type, bool all, bool async)
{
    requested = true;
    forced = true;
    renderAll |= all;
    displayHealth.forceUpdateType(type);

    // Normally, we need to start the timer, in case the display is busy and we
    // briefly defer the update
    if (async) {
        // We will run the thread as soon as we loop(),
        // after all Applets have had a chance to observe whatever event set this
        // off
        OSThread::setIntervalFromNow(0);
        OSThread::enabled = true;
        runASAP = true;
    }

    // If the update is *not* asynchronous, we begin the render process directly
    // here so that it can block code flow while running
    else
        render(false);
}

// Wait for any in-progress display update to complete before continuing
void InkHUD::Renderer::awaitUpdate()
{
    if (driver->busy()) {
        LOG_INFO("Waiting for display");
        driver->await(); // Wait here for update to complete
    }
}

// Set a ready-to-draw pixel into the image buffer
// All rotations / translations have already taken place: this buffer data is
// formatted ready for the driver
void InkHUD::Renderer::handlePixel(int16_t x, int16_t y, Color c)
{
    rotatePixelCoords(&x, &y);

    uint32_t byteNum = (y * imageBufferWidth) + (x / 8); // X data is 8 pixels per byte
    uint8_t bitNum = 7 - (x % 8);                        // Invert order: leftmost bit (most significant)
                                                         // is leftmost pixel of byte.

    bitWrite(imageBuffer[byteNum], bitNum, c);
#if INKHUD_BOOKREADER_GRAYSCALE
    if (grayBuffer)
        setGrayPixel(x, y, c == WHITE ? 15U : 0U);
#endif
}

// Width of the display, relative to rotation
uint16_t InkHUD::Renderer::width()
{
    if (settings->rotation % 2)
        return driver->height;
    else
        return driver->width;
}

// Height of the display, relative to rotation
uint16_t InkHUD::Renderer::height()
{
    if (settings->rotation % 2)
        return driver->width;
    else
        return driver->height;
}

// Runs at regular intervals
// - postponing render: until next loop(), allowing all applets to be notified
// of some Mesh event before render
// - queuing another render: while one is already is progress
int32_t InkHUD::Renderer::runOnce()
{
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    maintainRenderActivity();
#endif

    // If an applet asked to render, and hardware is able, lets try now
    if (requested && !driver->busy()) {
        render();
    }

    // If our render() call failed, try again shortly
    // otherwise, stop our thread until next update due
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    if (requested || keepAwakeUntilUpdateComplete)
#else
    if (requested)
#endif
        return 25UL;
    else
        return OSThread::disable();
}

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
void InkHUD::Renderer::maintainRenderActivity()
{
    if (keepAwakeUntilUpdateComplete) {
        // Give the reader a full idle timeout after the physical refresh finishes.
        powerFSM.trigger(EVENT_INPUT);
        keepAwakeUntilUpdateComplete = driver->busy();
        BookReaderApplet::setDisplayRefreshActive(keepAwakeUntilUpdateComplete);
#if INKHUD_BOOKREADER_PERF_LOG
        if (!keepAwakeUntilUpdateComplete)
            LOG_INFO("[BookReaderPerf] refresh END visible=1 ms=%lu timeoutRestarted=1",
                     static_cast<unsigned long>(millis() - readerRefreshStartMs));
#endif
    }
}
#endif

// Applies the system-wide rotation to pixel positions
// This step is applied to image data which has already been translated by a
// Tile object This is the final step before the pixel is placed into the image
// buffer No return: values of the *x and *y parameters are modified by the
// method
void InkHUD::Renderer::rotatePixelCoords(int16_t *x, int16_t *y)
{
    // Apply a global rotation to pixel locations
    int16_t x1 = 0;
    int16_t y1 = 0;
    switch (settings->rotation) {
    case 0:
        x1 = *x;
        y1 = *y;
        break;
    case 1:
        x1 = (driver->width - 1) - *y;
        y1 = *x;
        break;
    case 2:
        x1 = (driver->width - 1) - *x;
        y1 = (driver->height - 1) - *y;
        break;
    case 3:
        x1 = *y;
        y1 = (driver->height - 1) - *x;
        break;
    }
    *x = x1;
    *y = y1;
}

// Make an attempt to gather image data from some / all applets, and update the
// display Might not be possible right now, if update already is progress.
void InkHUD::Renderer::render(bool async)
{
    // Make sure the display is ready for a new update
    if (async) {
        // Previous update still running, Will try again shortly, via runOnce()
        if (driver->busy())
            return;
    } else {
        // Wait here for previous update to complete
        driver->await();
    }

    // Determine if a system applet has requested exclusive rights to request an
    // update, or exclusive rights to render
    checkLocks();

    // (Potentially) change applet to display new info,
    // then check if this newly displayed applet makes a pending notification
    // redundant
    inkhud->autoshow();

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    // Consume this request before drawing; an applet may queue its next loading
    // pass in onRender().
    requested = false;
#endif

    // If an update is justified.
    // We don't know this until after autoshow has run, as new applets may now be
    // in foreground
    if (shouldUpdate()) {

        // Decide which technique the display will use to change image
        // Done early, as rendering resets the Applets' requested types
        Drivers::EInk::UpdateTypes updateType = decideUpdateType();

        // Render the new image
        if (renderAll)
            clearBuffer();
        renderUserApplets();
        renderPlaceholders();
        renderSystemApplets();
#if INKHUD_BOOKREADER_GRAYSCALE
        bool readerVisible = false;
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
        if (!lockRendering)
            for (Applet *applet : inkhud->userApplets)
                if (applet && applet->isActive() && applet->isForeground() && applet->keepScreenAwakeDuringRender()) {
                    readerVisible = true;
                    break;
                }
#endif
        const bool useGrayscale = grayBuffer && (grayPixelCount || readerVisible);
        // Grayscale frames do not consume the monochrome buffer; rebuild it only on a monochrome transition.
        if (!useGrayscale && grayBuffer && monoBufferNeedsSync) {
            [[maybe_unused]] const uint32_t syncStartMs = millis();
            const size_t grayStride = (driver->width + 1U) / 2U;
            for (uint16_t y = 0; y < driver->height; ++y) {
                const uint8_t *row = grayBuffer + static_cast<size_t>(y) * grayStride;
                uint8_t *out = imageBuffer + static_cast<size_t>(y) * imageBufferWidth;
                for (uint16_t bx = 0; bx < imageBufferWidth; ++bx) {
                    uint8_t white = 0xFFU;
                    for (uint16_t bit = 0; bit < 8U && bx * 8U + bit < driver->width; ++bit) {
                        const uint16_t x = bx * 8U + bit;
                        if (((row[x / 2U] >> ((x & 1U) ? 0U : 4U)) & 15U) == 0U)
                            white &= ~(0x80U >> bit);
                    }
                    out[bx] = white;
                }
            }
            monoBufferNeedsSync = false;
#if INKHUD_BOOKREADER_PERF_LOG
            LOG_INFO("[BookReaderPerf] monochrome buffer restored ms=%lu", static_cast<unsigned long>(millis() - syncStartMs));
#endif
        }
        if (useGrayscale && updateType != Drivers::EInk::UpdateTypes::FULL) {
            displayHealth.forceUpdateType(Drivers::EInk::UpdateTypes::FULL);
            updateType = displayHealth.decideUpdateType();
        }
#endif

        // Invert Buffer if set by user
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
            for (size_t i = 0; i < imageBufferWidth * imageBufferHeight; ++i) {
                imageBuffer[i] = ~imageBuffer[i];
            }
#if INKHUD_BOOKREADER_GRAYSCALE
            if (useGrayscale)
                for (size_t i = 0; i < ((driver->width + 1U) / 2U) * driver->height; ++i)
                    grayBuffer[i] = ~grayBuffer[i];
#endif
        }

        // Tell display to begin process of drawing new image
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
        if (keepAwakeUntilUpdateComplete)
            BookReaderApplet::setDisplayRefreshActive(true);
#endif
        LOG_INFO("Updating display");
#if INKHUD_BOOKREADER_GRAYSCALE && INKHUD_BOOKREADER_PERF_LOG
        static uint32_t frameId = 0;
        LOG_INFO("[BookReaderDraw] submit frame=%lu type=%u gray=%u grayPixels=%lu "
                 "reader=%u renderAll=%u locked=%u inverted=%u",
                 static_cast<unsigned long>(++frameId), static_cast<unsigned>(updateType), useGrayscale,
                 static_cast<unsigned long>(grayPixelCount), readerVisible, renderAll, lockRendering,
                 config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED);
#endif
#if defined(MESHTASTIC_INCLUDE_INKHUD_BOOKREADER) && INKHUD_BOOKREADER_PERF_LOG
        if (keepAwakeUntilUpdateComplete) {
            readerRefreshStartMs = millis();
            LOG_INFO("[BookReaderPerf] refresh BEGIN type=%u async=%u", static_cast<unsigned>(updateType),
                     static_cast<unsigned>(async));
        }
#endif
#if INKHUD_BOOKREADER_GRAYSCALE
        if (useGrayscale)
            driver->updateGrayscale(grayBuffer);
        else
#endif
            driver->update(imageBuffer, updateType);

#if INKHUD_BOOKREADER_GRAYSCALE
        // Retained overlay renders must start from the original, uninverted pixels.
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
            driver->await();
            for (size_t i = 0; i < imageBufferWidth * imageBufferHeight; ++i)
                imageBuffer[i] = ~imageBuffer[i];
            if (useGrayscale)
                for (size_t i = 0; i < ((driver->width + 1U) / 2U) * driver->height; ++i)
                    grayBuffer[i] = ~grayBuffer[i];
        }
#endif

        // If not async, wait here until the update is complete
        if (!async)
            driver->await();

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
        maintainRenderActivity();
        if (keepAwakeUntilUpdateComplete) {
            OSThread::enabled = true;
            OSThread::setIntervalFromNow(25UL);
        }
#endif
    }

    // Our part is done now.
    // If update is async, the display hardware is still performing the update
    // process, but that's all handled by NicheGraphics::Drivers::EInk

    // Tidy up, ready for a new request
#ifndef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    requested = false;
#endif
    forced = false;
    renderAll = false;
}

// Manually fill the image buffer with WHITE
// Clears any old drawing
// Note: benchmarking revealed that this is *much* faster than setting pixels
// individually So much so that it's more efficient to re-render all applets,
// rather than rendering selectively, and manually blanking a portion of the
// display
void InkHUD::Renderer::clearBuffer()
{
#if INKHUD_BOOKREADER_GRAYSCALE && INKHUD_BOOKREADER_PERF_LOG
    LOG_INFO("[BookReaderDraw] clear framebuffer gray=%u", grayBuffer != nullptr);
#endif
    memset(imageBuffer, 0xFF, imageBufferHeight * imageBufferWidth);
#if INKHUD_BOOKREADER_GRAYSCALE
    if (grayBuffer)
        memset(grayBuffer, 0xFF, ((driver->width + 1U) / 2U) * driver->height);
    grayPixelCount = 0;
    monoBufferNeedsSync = false;
#endif
}

// Manually clear the pixels below a tile
void InkHUD::Renderer::clearTile(Tile *t)
{
    // Rotate the tile dimensions
    int16_t left = 0;
    int16_t top = 0;
    uint16_t tileW = 0;
    uint16_t tileH = 0;
    switch (settings->rotation) {
    case 0:
        left = t->getLeft();
        top = t->getTop();
        tileW = t->getWidth();
        tileH = t->getHeight();
        break;
    case 1:
        left = driver->width - (t->getTop() + t->getHeight());
        top = t->getLeft();
        tileW = t->getHeight();
        tileH = t->getWidth();
        break;
    case 2:
        left = driver->width - (t->getLeft() + t->getWidth());
        top = driver->height - (t->getTop() + t->getHeight());
        tileW = t->getWidth();
        tileH = t->getHeight();
        break;
    case 3:
        left = t->getTop();
        top = driver->height - (t->getLeft() + t->getWidth());
        tileW = t->getHeight();
        tileH = t->getWidth();
        break;
    }

    // Calculate the bounds to clear
    uint16_t xStart = (left < 0) ? 0 : left;
    uint16_t yStart = (top < 0) ? 0 : top;
    if (xStart >= driver->width || yStart >= driver->height || left + tileW < 0 || top + tileH < 0)
        return; // the box is completely off the screen
    uint16_t xEnd = left + tileW;
    uint16_t yEnd = top + tileH;
    if (xEnd > driver->width)
        xEnd = driver->width;
    if (yEnd > driver->height)
        yEnd = driver->height;
#if INKHUD_BOOKREADER_GRAYSCALE && INKHUD_BOOKREADER_PERF_LOG
    LOG_INFO("[BookReaderDraw] clear tile xy=%u,%u-%u,%u gray=%u", xStart, yStart, xEnd, yEnd, grayBuffer != nullptr);
#endif
#if INKHUD_BOOKREADER_GRAYSCALE
    if (grayBuffer) {
        if (xStart == 0 && yStart == 0 && xEnd == driver->width && yEnd == driver->height) {
            memset(grayBuffer, 0xFF, ((driver->width + 1U) / 2U) * driver->height);
            grayPixelCount = 0;
        } else {
            const size_t stride = (driver->width + 1U) / 2U;
            const size_t firstByte = (xStart + 1U) / 2U;
            const size_t endByte = xEnd / 2U;
            uint32_t clearedGrayPixels = 0;
            for (uint16_t y = yStart; y < yEnd; ++y) {
                if ((xStart & 1U) && xEnd > xStart)
                    setGrayPixel(xStart, y, 15U);
                if ((xEnd & 1U) && xEnd > xStart)
                    setGrayPixel(xEnd - 1U, y, 15U);
                uint8_t *row = grayBuffer + static_cast<size_t>(y) * stride;
                for (size_t x = firstByte; x < endByte; ++x) {
                    const uint8_t pair = row[x];
                    const uint8_t high = pair >> 4U;
                    const uint8_t low = pair & 15U;
                    clearedGrayPixels += high != 0U && high != 15U;
                    clearedGrayPixels += low != 0U && low != 15U;
                }
                if (endByte > firstByte)
                    memset(row + firstByte, 0xFF, endByte - firstByte);
            }
            grayPixelCount -= clearedGrayPixels;
        }
    }
#endif

    // Clear the pixels
    if (xStart == 0 && xEnd == driver->width) { // full width box is easier to clear
        memset(imageBuffer + (yStart * imageBufferWidth), 0xFF, (yEnd - yStart) * imageBufferWidth);
    } else {
        const uint16_t byteStart = (xStart / 8) + 1;
        const uint16_t byteEnd = xEnd / 8;
        const uint8_t leadingByte = 0xFF >> (xStart - ((byteStart - 1) * 8));
        const uint8_t trailingByte = (0xFF00 >> (xEnd - (byteEnd * 8))) & 0xFF;
        for (uint16_t i = yStart * imageBufferWidth; i < yEnd * imageBufferWidth; i += imageBufferWidth) {
            // Set the leading byte
            imageBuffer[i + byteStart - 1] |= leadingByte;

            // Set the continuous bytes
            if (byteStart < byteEnd)
                memset(imageBuffer + i + byteStart, 0xFF, byteEnd - byteStart);

            // Set the trailing byte
            if (byteEnd != imageBufferWidth)
                imageBuffer[i + byteEnd] |= trailingByte;
        }
    }
}

void InkHUD::Renderer::checkLocks()
{
    lockRendering = nullptr;
    lockRequests = nullptr;

    for (SystemApplet *sa : inkhud->systemApplets) {
        if (!lockRendering && sa->lockRendering && sa->isForeground()) {
            lockRendering = sa;
        }
        if (!lockRequests && sa->lockRequests && sa->isForeground()) {
            lockRequests = sa;
        }
    }
}

bool InkHUD::Renderer::shouldUpdate()
{
    bool should = false;

    // via forceUpdate
    should |= forced;

    // via a system applet (which has locked update requests)
    if (lockRequests) {
        should |= lockRequests->wantsToRender();
        return should; // Early exit - no other requests considered
    }

    // via system applet (not locked)
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->wantsToRender()    // This applet requested
            && sa->isForeground()) // This applet is currently shown
        {
            should = true;
            break;
        }
    }

    // via user applet
    for (Applet *ua : inkhud->userApplets) {
        if (ua                     // Tile has valid applet
            && ua->wantsToRender() // This applet requested display update
            && ua->isForeground()) // This applet is currently shown
        {
            should = true;
            break;
        }
    }

    return should;
}

// Determine which type of E-Ink update the display will perform, to change the
// image. Considers the needs of the various applets, then weighs against
// display health. An update type specified by forceUpdate will be granted with
// no further questioning.
Drivers::EInk::UpdateTypes InkHUD::Renderer::decideUpdateType()
{
    // Ask applets which update type they would prefer
    // Some update types take priority over others

    // No need to consider the "requests" if somebody already forced an update
    if (!forced) {
        // User applets
        for (Applet *ua : inkhud->userApplets) {
            if (ua && ua->isForeground() && (ua->wantsToRender() || renderAll))
                displayHealth.requestUpdateType(ua->wantsUpdateType());
        }
        // System Applets
        for (SystemApplet *sa : inkhud->systemApplets) {
            if (sa && sa->isForeground() && (sa->wantsToRender() || sa->alwaysRender || renderAll))
                displayHealth.requestUpdateType(sa->wantsUpdateType());
        }
    }

    return displayHealth.decideUpdateType();
}

// Run the drawing operations of any user applets which are currently displayed
// Pixel output is placed into the framebuffer, ready for handoff to the EInk
// driver
void InkHUD::Renderer::renderUserApplets()
{
    // Don't render user applets if a system applet has demanded the whole display
    // to itself
    if (lockRendering)
        return;

    // Render any user applets which are currently visible
    for (Applet *ua : inkhud->userApplets) {
        if (ua && ua->isActive() && ua->isForeground() && (ua->wantsToRender() || renderAll)) {

            // Clear the tile unless the applet wants to draw over its previous render
            // or everything is getting re-rendered anyways
            if (ua->wantsFullRender() && !renderAll)
                clearTile(ua->getTile());

            uint32_t start = millis();
            bool full = ua->wantsFullRender() || renderAll;
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
            if (ua->keepScreenAwakeDuringRender()) {
                keepAwakeUntilUpdateComplete = true;
                powerFSM.trigger(EVENT_INPUT);
            }
#endif
            ua->render(full); // Draw!
            uint32_t stop = millis();
            LOG_DEBUG("%s took %dms to render", ua->name, stop - start);
        }
    }
}

// Run the drawing operations of any system applets which are currently
// displayed Pixel output is placed into the framebuffer, ready for handoff to
// the EInk driver
void InkHUD::Renderer::renderSystemApplets()
{
    SystemApplet *battery = inkhud->getSystemApplet("BatteryIcon");
    SystemApplet *menu = inkhud->getSystemApplet("Menu");
    SystemApplet *notifications = inkhud->getSystemApplet("Notification");

    // Each system applet
    for (SystemApplet *sa : inkhud->systemApplets) {

        // Skip if not shown
        if (!sa->isForeground())
            continue;

        if (!sa->wantsToRender() && !sa->alwaysRender && !renderAll)
            continue;

        // Skip if locked by another applet
        if (lockRendering && lockRendering != sa)
            continue;

        // Don't draw the battery or notifications overtop the menu
        // Todo: smarter way to handle this
        if (menu->isForeground() && (sa == battery || sa == notifications))
            continue;

        assert(sa->getTile());

        // Clear the tile unless the applet wants to draw over its previous render
        // or everything is getting re-rendered anyways
        if (sa->wantsFullRender() && !renderAll)
            clearTile(sa->getTile());

        // uint32_t start = millis();
        bool full = sa->wantsFullRender() || renderAll;
        sa->render(full); // Draw!
                          // uint32_t stop = millis();
                          // LOG_DEBUG("%s took %dms to render", sa->name, stop - start);
    }
}

// In some situations (e.g. layout or applet selection changes),
// a user tile can end up without an assigned applet.
// In this case, we will fill the empty space with diagonal lines.
void InkHUD::Renderer::renderPlaceholders()
{
    // Don't fill empty space with placeholders if a system applet wants exclusive
    // use of the display
    if (lockRendering)
        return;

    // Ask the window manager which tiles are empty
    std::vector<Tile *> emptyTiles = inkhud->getEmptyTiles();

    // No empty tiles
    if (emptyTiles.size() == 0)
        return;

    SystemApplet *placeholder = inkhud->getSystemApplet("Placeholder");

    // uint32_t start = millis();
    for (Tile *t : emptyTiles) {
        t->assignApplet(placeholder);
        // Clear the tile unless everything is getting re-rendered
        if (!renderAll)
            clearTile(t);
        placeholder->render(true); // full render
        t->assignApplet(nullptr);
    }
    // uint32_t stop = millis();
    // LOG_DEBUG("Placeholders took %dms to render", stop - start);
}

#endif
