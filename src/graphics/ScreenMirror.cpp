#include "ScreenMirror.h"

#if HAS_SCREEN_MIRROR

#include "DebugConfiguration.h"
#include "Screen.h"
#include "concurrency/LockGuard.h"
#include "memory/MemAudit.h"
#include <OLEDDisplay.h>
#include <cstring>

#include "TFTColorRegions.h"

namespace graphics
{

ScreenMirror screenMirror;

namespace
{
constexpr const char *kMemTag = "mirror";

// panel byte order (big-endian RGB565) to the wire's logical layout
inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}
} // namespace

void ScreenMirror::freeSnapshotLocked()
{
    if (snapshot) {
        memaudit::add(kMemTag, -(int32_t)frameSize);
        free(snapshot);
        snapshot = nullptr;
    }
    frameSize = 0;
    if (paletteRegions) {
        memaudit::add(kMemTag, -(int32_t)(sizeof(PaletteRegion) * MAX_TFT_COLOR_REGIONS));
        free(paletteRegions);
        paletteRegions = nullptr;
    }
    paletteSig = 0;
    paletteCount = 0;
#if HAS_MUI_MIRROR
    if (muiPool) {
        if (!muiPoolInPsram)
            memaudit::add(kMemTag, -(int32_t)MUI_POOL_BYTES);
        free(muiPool);
        muiPool = nullptr;
    }
    muiHead = muiCount = 0;
    muiPoolUsed = 0;
    muiRectSendOffset = 0;
    muiOwner = nullptr;
#endif
}

void ScreenMirror::releaseLocked(const void *client)
{
#if HAS_MUI_MIRROR
    if (muiOwner == client) {
        muiOwner = nullptr;
        muiRectSendOffset = 0; // the next owner starts the head rect from its first byte
    }
#endif
    freeIfIdleLocked();
}

void ScreenMirror::freeIfIdleLocked()
{
    if (armedLocked())
        return;
    captureRequested = false;
    freeSnapshotLocked();
}

// Called outside the lock: the screen thread captures on its next pass, even while the panel is off.
void ScreenMirror::kickCapture()
{
#if HAS_MUI_MIRROR
    if (muiRefresh)
        muiRefresh();
#endif
    if (screen)
        screen->kick();
}

void ScreenMirror::subscribe()
{
    {
        concurrency::LockGuard g(&lock);
        if (subscribers == UINT8_MAX)
            return;
        subscribers++;
        captureRequested = true;
    }
    kickCapture();
}

void ScreenMirror::unsubscribe(const void *client)
{
    concurrency::LockGuard g(&lock);
    if (subscribers)
        subscribers--;
    releaseLocked(client);
}

void ScreenMirror::requestFrame()
{
    {
        concurrency::LockGuard g(&lock);
        if (pendingRequests == UINT8_MAX)
            return;
        pendingRequests++;
        captureRequested = true;
    }
    kickCapture();
}

void ScreenMirror::frameRequestDone(const void *client)
{
    concurrency::LockGuard g(&lock);
    if (pendingRequests)
        pendingRequests--;
    releaseLocked(client);
}

void ScreenMirror::cursorAtHead(uint32_t &clientFrameId, uint16_t &clientOffset)
{
    concurrency::LockGuard g(&lock);
    clientFrameId = frameId;
    clientOffset = frameSize;
}

void ScreenMirror::onRendered(OLEDDisplay *display)
{
    uint32_t readyId = 0;
    {
        concurrency::LockGuard g(&lock);
        if (!armedLocked())
            return;
        if (!display || !display->buffer)
            return;

        uint16_t w = display->getWidth();
        uint16_t h = display->getHeight();
        uint32_t fullSize = (uint32_t)w * ((h + 7) / 8);
        if (fullSize == 0)
            return;
        if (fullSize > UINT16_MAX) {
            LOG_WARN("Screen mirror: %ux%u framebuffer too large to stream", w, h);
            subscribers = pendingRequests = 0;
            freeIfIdleLocked();
            return;
        }
        uint16_t size = (uint16_t)fullSize;

        if (snapshot && size != frameSize) {
            memaudit::add(kMemTag, -(int32_t)frameSize);
            free(snapshot);
            snapshot = nullptr;
        }

        bool firstFrame = !snapshot;
        if (firstFrame) {
            snapshot = (uint8_t *)malloc(size);
            if (!snapshot) {
                LOG_ERROR("Screen mirror: no memory for %u byte snapshot", size);
                subscribers = pendingRequests = 0;
                freeIfIdleLocked();
                return;
            }
            memaudit::add(kMemTag, size);
            frameSize = size;
            width = w;
            height = h;
        }

        // a recolor alone is frame-worthy: clients key colors off the frame's signature
        bool paletteChanged = snapshotPaletteSig != paletteSig;
        if (!firstFrame && !captureRequested && !paletteChanged && memcmp(display->buffer, snapshot, frameSize) == 0)
            return;

        memcpy(snapshot, display->buffer, frameSize);
        snapshotPaletteSig = paletteSig;
        frameId++;
        captureRequested = false;
        readyId = frameId;
    }
    frameReady.notifyObservers(readyId);
}

void ScreenMirror::capturePalette(uint32_t signature, uint16_t defaultOnBe, uint16_t defaultOffBe, const TFTColorRegion *regions,
                                  uint8_t count)
{
    // regions alone do not change on a recolor, so the theme defaults are part of the signature
    signature ^= (((uint32_t)defaultOnBe << 16) | defaultOffBe) * 2654435761u;
    concurrency::LockGuard g(&lock);
    if (!armedLocked())
        return;
    if (signature == paletteSig && paletteRegions)
        return;
    if (!paletteRegions) {
        paletteRegions = (PaletteRegion *)malloc(sizeof(PaletteRegion) * MAX_TFT_COLOR_REGIONS);
        if (!paletteRegions)
            return; // frames still stream; clients render monochrome
        memaudit::add(kMemTag, sizeof(PaletteRegion) * MAX_TFT_COLOR_REGIONS);
    }
    if (count > MAX_TFT_COLOR_REGIONS)
        count = MAX_TFT_COLOR_REGIONS;
    for (uint8_t i = 0; i < count; i++) {
        const TFTColorRegion &r = regions[i];
        paletteRegions[i] = {(uint16_t)r.x,      (uint16_t)r.y,       (uint16_t)r.width,
                             (uint16_t)r.height, swap16(r.onColorBe), swap16(r.offColorBe)};
    }
    paletteCount = count;
    paletteDefaultOn = swap16(defaultOnBe);
    paletteDefaultOff = swap16(defaultOffBe);
    paletteSig = signature;
}

bool ScreenMirror::hasPaletteChunkFor(uint32_t clientPaletteSig, uint8_t clientRegionOffset)
{
    concurrency::LockGuard g(&lock);
    if (!paletteRegions || !snapshot)
        return false;
    return clientPaletteSig != paletteSig || clientRegionOffset < paletteCount;
}

bool ScreenMirror::copyPaletteChunk(uint32_t &clientPaletteSig, uint8_t &clientRegionOffset, meshtastic_DisplayPalette &out)
{
    concurrency::LockGuard g(&lock);
    if (!paletteRegions || !snapshot)
        return false;
    if (clientPaletteSig != paletteSig) {
        clientPaletteSig = paletteSig;
        clientRegionOffset = 0;
    } else if (clientRegionOffset >= paletteCount) {
        return false;
    }

    out.signature = paletteSig;
    out.default_on_color = paletteDefaultOn;
    out.default_off_color = paletteDefaultOff;
    out.region_offset = clientRegionOffset;
    out.region_total = paletteCount;
    uint8_t n = 0;
    while (n < (sizeof(out.regions) / sizeof(out.regions[0])) && clientRegionOffset + n < paletteCount) {
        const PaletteRegion &r = paletteRegions[clientRegionOffset + n];
        out.regions[n].x = r.x;
        out.regions[n].y = r.y;
        out.regions[n].width = r.w;
        out.regions[n].height = r.h;
        out.regions[n].on_color = r.onColor;
        out.regions[n].off_color = r.offColor;
        n++;
    }
    out.regions_count = n;
    clientRegionOffset += n;
    return true;
}

#if HAS_MUI_MIRROR
void ScreenMirror::onMuiRect(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t stride)
{
    uint32_t readyId = 0;
    {
        concurrency::LockGuard g(&lock);
        if (!armedLocked())
            return;
        if (x < 0 || y < 0 || w == 0 || h == 0 || muiPanelW == 0 || stride < w)
            return;

        uint32_t bytes = (uint32_t)w * h * 2;
        if (!muiPool) {
#ifdef ESP32
            muiPool = (uint8_t *)ps_malloc(MUI_POOL_BYTES);
#endif
            muiPoolInPsram = muiPool != nullptr;
            if (!muiPool)
                muiPool = (uint8_t *)malloc(MUI_POOL_BYTES);
            if (!muiPool) {
                LOG_ERROR("Screen mirror: no memory for the %u byte rect pool", (unsigned)MUI_POOL_BYTES);
                subscribers = pendingRequests = 0;
                freeIfIdleLocked();
                return;
            }
            if (!muiPoolInPsram)
                memaudit::add(kMemTag, MUI_POOL_BYTES); // PSRAM is not the constrained budget
        }
        if (bytes > MUI_POOL_BYTES)
            return; // a rect larger than the whole pool can never be sent
        if (muiCount >= MUI_MAX_RECTS || muiPoolUsed + bytes > MUI_POOL_BYTES) {
            // consumer is behind: drop the backlog, not the newest pixels, and repaint once
            muiHead = muiCount = 0;
            muiPoolUsed = 0;
            muiRectSendOffset = 0;
            if (muiRefresh)
                muiRefresh();
            return;
        }
        uint8_t *dst = muiPool + muiPoolUsed;
        if (!muiByteSwapped && stride == w) {
            memcpy(dst, pixels, bytes);
        } else {
            for (uint16_t row = 0; row < h; row++) {
                const uint16_t *src = pixels + (uint32_t)row * stride;
                for (uint16_t col = 0; col < w; col++) {
                    uint16_t v = muiByteSwapped ? swap16(src[col]) : src[col];
                    *dst++ = (uint8_t)(v & 0xFF);
                    *dst++ = (uint8_t)(v >> 8);
                }
            }
        }
        MuiRect &r = muiRects[(muiHead + muiCount) % MUI_MAX_RECTS];
        r = {(uint16_t)x, (uint16_t)y, w, h, bytes, muiPoolUsed, ++frameId};
        muiPoolUsed += bytes;
        muiCount++;
        // only empty -> non-empty needs a wakeup; available() keeps the client draining
        if (muiCount == 1)
            readyId = frameId;
    }
    if (readyId)
        frameReady.notifyObservers(readyId);
}

// Fills one chunk of the oldest queued rect; pops it when fully drained.
bool ScreenMirror::copyMuiChunkLocked(uint32_t &clientFrameId, meshtastic_DisplayFrame &out)
{
    MuiRect &r = muiRects[muiHead];
    clientFrameId = r.id;
    uint32_t len = r.bytes - muiRectSendOffset;
    if (len > sizeof(out.data.bytes))
        len = sizeof(out.data.bytes);

    out.width = muiPanelW;
    out.height = muiPanelH;
    out.format = meshtastic_DisplayFrame_Format_RGB565;
    out.palette_signature = 0;
    out.frame_id = r.id;
    out.rect_x = r.x;
    out.rect_y = r.y;
    out.rect_width = r.w;
    out.rect_height = r.h;
    out.offset = muiRectSendOffset;
    out.total_size = r.bytes;
    out.data.size = len;
    memcpy(out.data.bytes, muiPool + r.poolOffset + muiRectSendOffset, len);
    muiRectSendOffset += len;

    if (muiRectSendOffset >= r.bytes) {
        muiHead = (muiHead + 1) % MUI_MAX_RECTS;
        muiCount--;
        muiRectSendOffset = 0;
        if (muiCount == 0)
            muiPoolUsed = 0;
    }
    return true;
}
#endif

bool ScreenMirror::hasChunkFor(const void *client, uint32_t clientFrameId, uint16_t clientOffset)
{
    concurrency::LockGuard g(&lock);
#if HAS_MUI_MIRROR
    // one shared rect cursor: another connection sees no rects rather than half of each
    if (muiCount && (muiOwner == nullptr || muiOwner == client))
        return true;
#endif
    return snapshot && (clientFrameId != frameId || clientOffset < frameSize);
}

bool ScreenMirror::copyChunk(const void *client, uint32_t &clientFrameId, uint16_t &clientOffset, meshtastic_DisplayFrame &out)
{
    concurrency::LockGuard g(&lock);
#if HAS_MUI_MIRROR
    if (muiCount) {
        if (muiOwner == nullptr)
            muiOwner = client; // first drainer claims the stream
        if (muiOwner != client)
            return false;
        return copyMuiChunkLocked(clientFrameId, out);
    }
#endif
    if (!snapshot)
        return false;
    if (clientFrameId != frameId) {
        clientFrameId = frameId;
        clientOffset = 0;
    }
    if (clientOffset >= frameSize)
        return false;

    uint16_t len = frameSize - clientOffset;
    if (len > sizeof(out.data.bytes))
        len = sizeof(out.data.bytes);

    out.width = width;
    out.height = height;
    out.format = meshtastic_DisplayFrame_Format_MONO_VLSB;
    out.palette_signature = snapshotPaletteSig;
    out.frame_id = frameId;
    out.offset = clientOffset;
    out.total_size = frameSize;
    out.data.size = len;
    memcpy(out.data.bytes, snapshot + clientOffset, len);
    clientOffset += len;
    return true;
}

} // namespace graphics

#endif
