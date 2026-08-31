#include "ScreenMirror.h"

#if HAS_SCREEN_MIRROR

#include "DebugConfiguration.h"
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
// Region colors arrive panel-byte-order (big-endian RGB565); the wire
// carries logical bit layout.
inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}
} // namespace

void ScreenMirror::freeSnapshotLocked()
{
    if (snapshot) {
        memaudit::add("display", -(int32_t)frameSize);
        free(snapshot);
        snapshot = nullptr;
    }
    frameSize = 0;
    if (paletteRegions) {
        memaudit::add("display", -(int32_t)(sizeof(PaletteRegion) * MAX_TFT_COLOR_REGIONS));
        free(paletteRegions);
        paletteRegions = nullptr;
    }
    paletteSig = 0;
    paletteCount = 0;
}

void ScreenMirror::setMirror(bool enabled)
{
    concurrency::LockGuard g(&lock);
    if (mirroring == enabled && !(enabled && oneShot))
        return; // no state change; keeps client disconnects from logging forever
    mirroring = enabled;
    if (enabled) {
        // Force an immediate frame so the client doesn't wait for the next
        // on-screen change.
        oneShot = true;
    } else {
        oneShot = false;
        freeSnapshotLocked();
    }
    LOG_INFO("Screen mirror %s", enabled ? "enabled" : "disabled");
}

void ScreenMirror::requestFrame()
{
    concurrency::LockGuard g(&lock);
    oneShot = true;
}

void ScreenMirror::onRendered(OLEDDisplay *display)
{
    uint32_t readyId = 0;
    {
        concurrency::LockGuard g(&lock);
        if (!mirroring && !oneShot)
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
            mirroring = oneShot = false;
            return;
        }
        uint16_t size = (uint16_t)fullSize;

        if (snapshot && size != frameSize)
            freeSnapshotLocked(); // display geometry changed; start over

        bool firstFrame = !snapshot;
        if (firstFrame) {
            snapshot = (uint8_t *)malloc(size);
            if (!snapshot) {
                LOG_ERROR("Screen mirror: no memory for %u byte snapshot", size);
                mirroring = oneShot = false;
                return;
            }
            memaudit::add("display", size);
            frameSize = size;
            width = w;
            height = h;
        }

        // A palette-only change (theme recolor) is frame-worthy even when the
        // mono bits are identical: the client keys colors off the frame's signature.
        bool paletteChanged = snapshotPaletteSig != paletteSig;
        if (!firstFrame && !oneShot && !paletteChanged && memcmp(display->buffer, snapshot, frameSize) == 0)
            return;

        memcpy(snapshot, display->buffer, frameSize);
        snapshotPaletteSig = paletteSig;
        frameId++;
        oneShot = false;
        readyId = frameId;
    }
    // Notify outside the lock: observers (PhoneAPI) may re-enter hasChunkFor.
    frameReady.notifyObservers(readyId);
}

void ScreenMirror::capturePalette(uint32_t signature, uint16_t defaultOnBe, uint16_t defaultOffBe, const TFTColorRegion *regions,
                                  uint8_t count)
{
    concurrency::LockGuard g(&lock);
    // Cheap when nothing changed and when the mirror is idle: paint-time
    // callers hit this every frame, but clients only exist while mirroring.
    if (!mirroring && !oneShot && !snapshot)
        return;
    if (signature == paletteSig && paletteRegions)
        return;
    if (!paletteRegions) {
        paletteRegions = (PaletteRegion *)malloc(sizeof(PaletteRegion) * MAX_TFT_COLOR_REGIONS);
        if (!paletteRegions)
            return; // frames still stream; clients render monochrome
        memaudit::add("display", sizeof(PaletteRegion) * MAX_TFT_COLOR_REGIONS);
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

bool ScreenMirror::hasChunkFor(uint32_t clientFrameId, uint16_t clientOffset)
{
    concurrency::LockGuard g(&lock);
    return snapshot && (clientFrameId != frameId || clientOffset < frameSize);
}

bool ScreenMirror::copyChunk(uint32_t &clientFrameId, uint16_t &clientOffset, meshtastic_DisplayFrame &out)
{
    concurrency::LockGuard g(&lock);
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
    // The signature captured WITH this snapshot, not the live one: a drain can
    // span a display() that already advanced paletteSig for the next frame.
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
