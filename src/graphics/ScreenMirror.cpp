#include "ScreenMirror.h"

#if HAS_SCREEN_MIRROR

#include "DebugConfiguration.h"
#include "concurrency/LockGuard.h"
#include "memory/MemAudit.h"
#include <OLEDDisplay.h>
#include <cstring>

namespace graphics
{

ScreenMirror screenMirror;

void ScreenMirror::freeSnapshotLocked()
{
    if (snapshot) {
        memaudit::add("display", -(int32_t)frameSize);
        free(snapshot);
        snapshot = nullptr;
    }
    frameSize = 0;
}

void ScreenMirror::setMirror(bool enabled)
{
    concurrency::LockGuard g(&lock);
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
        uint16_t size = w * ((h + 7) / 8);
        if (size == 0)
            return;

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

        if (!firstFrame && !oneShot && memcmp(display->buffer, snapshot, frameSize) == 0)
            return;

        memcpy(snapshot, display->buffer, frameSize);
        frameId++;
        oneShot = false;
        readyId = frameId;
    }
    // Notify outside the lock: observers (PhoneAPI) may re-enter hasChunkFor.
    frameReady.notifyObservers(readyId);
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
