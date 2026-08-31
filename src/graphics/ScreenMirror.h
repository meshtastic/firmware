#pragma once
#include "configuration.h"

#if HAS_SCREEN_MIRROR

#include "Observer.h"
#include "concurrency/Lock.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class OLEDDisplay;

namespace graphics
{

/**
 * Streams 1bpp framebuffer snapshots to local clients as
 * FromRadio.display_frame chunks. Armed via AdminMessage
 * set_display_mirror (continuous) or get_display_frame_request (one-shot).
 *
 * Holds only the latest captured frame; each PhoneAPI instance keeps its own
 * (frameId, offset) drain cursor and pulls chunks via copyChunk, so multiple
 * clients receive complete frames independently.
 */
class ScreenMirror
{
  public:
    /// Fired when a new frame is ready to drain; PhoneAPI observes this.
    Observable<uint32_t> frameReady;

    void setMirror(bool enabled);
    void requestFrame();

    /// Called by Screen after each frame commit. Snapshots the framebuffer
    /// when armed and the contents changed since the last captured frame.
    void onRendered(OLEDDisplay *display);

    /// True while the client cursor has undelivered bytes of the current frame.
    bool hasChunkFor(uint32_t clientFrameId, uint16_t clientOffset);

    /// Fills the next chunk for a client cursor, advancing it; false when the
    /// client is fully caught up (or no frame exists). A frame captured while
    /// the client was mid-drain restarts it at offset 0 of the new frame.
    bool copyChunk(uint32_t &clientFrameId, uint16_t &clientOffset, meshtastic_DisplayFrame &out);

  private:
    void freeSnapshotLocked();

    concurrency::Lock lock;
    bool mirroring = false;
    bool oneShot = false;
    // Latest captured frame; doubles as the change-detection baseline.
    uint8_t *snapshot = nullptr;
    uint16_t frameSize = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t frameId = 0;
};

extern ScreenMirror screenMirror;

} // namespace graphics

#endif
