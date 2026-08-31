#pragma once
#include "configuration.h"

#if HAS_SCREEN && !defined(MESHTASTIC_EXCLUDE_SCREEN_MIRROR)

#include "Observer.h"
#include "concurrency/Lock.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class OLEDDisplay;

namespace graphics
{

/**
 * Streams 1bpp framebuffer snapshots to the local client as
 * FromRadio.display_frame chunks. Armed via AdminMessage
 * set_display_mirror (continuous) or get_display_frame_request (one-shot).
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

    /// Fills the next pending chunk for the phone; false when drained.
    bool getChunkForPhone(meshtastic_DisplayFrame &out);
    bool hasChunkForPhone();

  private:
    concurrency::Lock lock;
    bool mirroring = false;
    bool oneShot = false;
    // Last captured frame; doubles as the change-detection baseline.
    uint8_t *snapshot = nullptr;
    uint16_t frameSize = 0;
    uint16_t sendOffset = 0; // == frameSize when drained
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t frameId = 0;
};

extern ScreenMirror screenMirror;

} // namespace graphics

#endif
