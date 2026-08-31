#pragma once
#include "configuration.h"

#if HAS_SCREEN_MIRROR

#include "Observer.h"
#include "concurrency/Lock.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class OLEDDisplay;

namespace graphics
{

struct TFTColorRegion;

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

    /// Called by the color display drivers at paint time, before they clear
    /// the per-frame region table: stores the palette the frame was painted
    /// with. Colors arrive panel-byte-order (big-endian RGB565).
    void capturePalette(uint32_t signature, uint16_t defaultOnBe, uint16_t defaultOffBe, const TFTColorRegion *regions,
                        uint8_t count);

    /// True while the client cursor lacks regions of the current color palette.
    bool hasPaletteChunkFor(uint32_t clientPaletteSig, uint8_t clientRegionOffset);

    /// Fills the next palette chunk for a client cursor, advancing it; false
    /// when the client holds the full current palette (or coloring is off).
    bool copyPaletteChunk(uint32_t &clientPaletteSig, uint8_t &clientRegionOffset, meshtastic_DisplayPalette &out);

#if HAS_TFT
    /// MUI path: queues one LVGL dirty rect (native little-endian RGB565).
    /// Called on the LVGL thread via the device-ui flush observer; copies and returns.
    void onMuiRect(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);

    /// Registers device-ui's thread-safe full-repaint request, used to
    /// synchronize a newly armed client and to recover from queue overflow.
    using FullRefreshFn = void (*)();
    void setMuiRefresh(FullRefreshFn fn) { muiRefresh = fn; }
#endif

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
    // Signature of the palette the current snapshot was painted with; frames
    // carry this, not the live paletteSig, so mid-drain captures stay coherent.
    uint32_t snapshotPaletteSig = 0;
    // Color-region palette captured at paint time (TFT/HUB75 builds only).
    uint32_t paletteSig = 0;
    uint8_t paletteCount = 0;
    struct PaletteRegion {
        uint16_t x, y, w, h;
        uint16_t onColor, offColor; // logical RGB565
    };
    PaletteRegion *paletteRegions = nullptr;
    uint16_t paletteDefaultOn = 0;
    uint16_t paletteDefaultOff = 0;

#if HAS_TFT
    // MUI dirty-rect queue: FIFO rect headers over a linear pixel pool,
    // compacted whenever it drains. Spike scope: single consumer.
    struct MuiRect {
        uint16_t x, y, w, h;
        uint32_t bytes;
        uint32_t poolOffset;
        uint32_t id;
    };
    static constexpr uint8_t MUI_MAX_RECTS = 64;
    // Must hold one full repaint (320x240 RGB565 = 150 KB) plus concurrent
    // incremental rects, or arming can never deliver a complete first frame.
    static constexpr uint32_t MUI_POOL_BYTES = 192 * 1024;
    MuiRect muiRects[MUI_MAX_RECTS];
    uint8_t muiHead = 0;
    uint8_t muiCount = 0;
    uint8_t *muiPool = nullptr;
    uint32_t muiPoolUsed = 0;
    uint32_t muiRectSendOffset = 0;
    bool muiNeedResync = false;
    uint16_t muiPanelW = 0;
    uint16_t muiPanelH = 0;
    FullRefreshFn muiRefresh = nullptr;

    bool copyMuiChunkLocked(meshtastic_DisplayFrame &out);
#endif
};

extern ScreenMirror screenMirror;

#if HAS_TFT
/**
 * Routes a remote input event straight into device-ui's injection seam.
 * MUI builds never construct an InputBroker (Modules.cpp skips it when
 * displaymode is COLOR), so admin input cannot travel the usual path.
 * Returns false when MUI is not the active UI.
 */
bool muiInjectInputEvent(uint32_t eventCode, uint32_t kbChar, uint32_t touchX, uint32_t touchY);

/** Fills MUI's panel geometry for DeviceMetadata; false when MUI is not active. */
bool muiDisplayInfo(uint16_t &width, uint16_t &height, bool &hasTouch);
#endif

} // namespace graphics

#endif
