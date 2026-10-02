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
 * Streams the framebuffer to the local clients that ask for it, as FromRadio display_frame chunks.
 * Holds only the latest frame; each PhoneAPI keeps its own drain cursor.
 */
class ScreenMirror
{
  public:
    /// Fired when a new frame is ready to drain; PhoneAPI observes this.
    Observable<uint32_t> frameReady;

    /// A client starts or stops a continuous stream. Capture runs while any client is subscribed.
    void subscribe();
    void unsubscribe(const void *client);

    /// A client asks for one frame; it calls frameRequestDone() once that frame is delivered.
    void requestFrame();
    void frameRequestDone(const void *client);

    /// Places a cursor after the current frame, so a new consumer only receives frames captured from now on.
    void cursorAtHead(uint32_t &clientFrameId, uint16_t &clientOffset);

    /// Called by Screen after each frame commit; snapshots the framebuffer when it changed.
    void onRendered(OLEDDisplay *display);

    /// True while this client has undelivered bytes of the current frame.
    bool hasChunkFor(const void *client, uint32_t clientFrameId, uint16_t clientOffset);

    /// Fills the next chunk for a client cursor and advances it. A newer frame restarts the cursor at its offset 0.
    bool copyChunk(const void *client, uint32_t &clientFrameId, uint16_t &clientOffset, meshtastic_DisplayFrame &out);

    /// Called by the color display drivers before they clear the region table. Colors are panel byte order.
    void capturePalette(uint32_t signature, uint16_t defaultOnBe, uint16_t defaultOffBe, const TFTColorRegion *regions,
                        uint8_t count);

    bool hasPaletteChunkFor(uint32_t clientPaletteSig, uint8_t clientRegionOffset);
    bool copyPaletteChunk(uint32_t &clientPaletteSig, uint8_t &clientRegionOffset, meshtastic_DisplayPalette &out);

#if HAS_MUI_MIRROR
    /// LVGL thread: queues one dirty rect as tightly packed little-endian RGB565.
    void onMuiRect(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t stride);

    using FullRefreshFn = void (*)();
    void setMuiSource(FullRefreshFn fn, uint16_t panelWidth, uint16_t panelHeight, bool byteSwapped)
    {
        muiRefresh = fn;
        muiPanelW = panelWidth;
        muiPanelH = panelHeight;
        muiByteSwapped = byteSwapped;
    }
#endif

  private:
    bool armedLocked() const { return subscribers || pendingRequests; }
    void kickCapture();
    void releaseLocked(const void *client);
    void freeIfIdleLocked();
    void freeSnapshotLocked();

    concurrency::Lock lock;
    uint8_t subscribers = 0;
    uint8_t pendingRequests = 0;
    bool captureRequested = false;
    // Latest captured frame; doubles as the change-detection baseline.
    uint8_t *snapshot = nullptr;
    uint16_t frameSize = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t frameId = 0;
    // The palette this snapshot was painted with; a drain can outlive the live paletteSig.
    uint32_t snapshotPaletteSig = 0;
    uint32_t paletteSig = 0;
    uint8_t paletteCount = 0;
    struct PaletteRegion {
        uint16_t x, y, w, h;
        uint16_t onColor, offColor; // logical RGB565
    };
    PaletteRegion *paletteRegions = nullptr;
    uint16_t paletteDefaultOn = 0;
    uint16_t paletteDefaultOff = 0;

#if HAS_MUI_MIRROR
    // FIFO rect headers over a linear pixel pool, compacted whenever it drains; one consumer at a time.
    struct MuiRect {
        uint16_t x, y, w, h;
        uint32_t bytes;
        uint32_t poolOffset;
        uint32_t id;
    };
    static constexpr uint8_t MUI_MAX_RECTS = 64;
    // Holds a full 320x240 repaint plus incremental rects, or no first frame can complete.
    static constexpr uint32_t MUI_POOL_BYTES = 192 * 1024;
    MuiRect muiRects[MUI_MAX_RECTS];
    uint8_t muiHead = 0;
    uint8_t muiCount = 0;
    uint8_t *muiPool = nullptr;
    uint32_t muiPoolUsed = 0;
    uint32_t muiRectSendOffset = 0;
    uint16_t muiPanelW = 0;
    uint16_t muiPanelH = 0;
    bool muiByteSwapped = false;
    bool muiPoolInPsram = false;
    FullRefreshFn muiRefresh = nullptr;
    const void *muiOwner = nullptr; // connection currently draining rects

    bool copyMuiChunkLocked(uint32_t &clientFrameId, meshtastic_DisplayFrame &out);
#endif
};

extern ScreenMirror screenMirror;

#if HAS_MUI_MIRROR
/// MUI builds construct no InputBroker, so remote input goes straight to device-ui. False when MUI is not active.
bool muiInjectInputEvent(uint32_t eventCode, uint32_t kbChar, uint32_t touchX, uint32_t touchY);

/** Fills MUI's panel geometry for DeviceMetadata; false when MUI is not active. */
bool muiDisplayInfo(uint16_t &width, uint16_t &height, bool &hasTouch);
#endif

} // namespace graphics

#endif
