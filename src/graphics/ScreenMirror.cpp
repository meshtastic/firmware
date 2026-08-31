#include "ScreenMirror.h"

#if HAS_SCREEN && !defined(MESHTASTIC_EXCLUDE_SCREEN_MIRROR)

#include "DebugConfiguration.h"
#include "concurrency/LockGuard.h"
#include <OLEDDisplay.h>
#include <cstring>

namespace graphics
{

ScreenMirror screenMirror;

void ScreenMirror::setMirror(bool enabled)
{
    concurrency::LockGuard g(&lock);
    mirroring = enabled;
    // Force an immediate frame on enable so the client doesn't wait for the
    // next on-screen change.
    if (enabled)
        oneShot = true;
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

        // Don't overwrite a frame the client is still draining; the next
        // render picks up the latest contents.
        if (snapshot && sendOffset < frameSize)
            return;

        uint16_t w = display->getWidth();
        uint16_t h = display->getHeight();
        uint16_t size = w * ((h + 7) / 8);
        if (size == 0)
            return;

        if (!snapshot) {
            snapshot = (uint8_t *)malloc(size);
            if (!snapshot) {
                LOG_ERROR("Screen mirror: no memory for %u byte snapshot", size);
                mirroring = oneShot = false;
                return;
            }
            frameSize = size;
            sendOffset = size; // drained; nothing captured yet
            width = w;
            height = h;
            // Invalidate the change-detect baseline so the first frame always sends
            memset(snapshot, 0xA5, size);
        }

        bool changed = memcmp(display->buffer, snapshot, frameSize) != 0;
        if (!changed && !oneShot)
            return;

        memcpy(snapshot, display->buffer, frameSize);
        sendOffset = 0;
        frameId++;
        oneShot = false;
        readyId = frameId;
    }
    // Notify outside the lock: observers (PhoneAPI) may re-enter hasChunkForPhone.
    frameReady.notifyObservers(readyId);
}

bool ScreenMirror::hasChunkForPhone()
{
    concurrency::LockGuard g(&lock);
    return snapshot && sendOffset < frameSize;
}

bool ScreenMirror::getChunkForPhone(meshtastic_DisplayFrame &out)
{
    concurrency::LockGuard g(&lock);
    if (!snapshot || sendOffset >= frameSize)
        return false;

    uint16_t len = frameSize - sendOffset;
    if (len > sizeof(out.data.bytes))
        len = sizeof(out.data.bytes);

    out.width = width;
    out.height = height;
    out.format = meshtastic_DisplayFrame_Format_MONO_VLSB;
    out.frame_id = frameId;
    out.offset = sendOffset;
    out.total_size = frameSize;
    out.data.size = len;
    memcpy(out.data.bytes, snapshot + sendOffset, len);
    sendOffset += len;
    return true;
}

} // namespace graphics

#endif
