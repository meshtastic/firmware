#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Orchestrates updating of the display image

- takes requests (or demands) for display update
- performs the various steps of the rendering operation
- interfaces with the E-Ink driver

*/

#pragma once

#include "configuration.h"

#include "./DisplayHealth.h"
#include "./InkHUD.h"
#include "./Persistence.h"
#include "graphics/niche/Drivers/EInk/EInk.h"

namespace NicheGraphics::InkHUD
{

class Renderer : protected concurrency::OSThread
{

  public:
    Renderer();

    // Configuration, before begin

    void setDriver(Drivers::EInk *driver);
    void setDisplayResilience(uint8_t fastPerFull, float stressMultiplier);

    void begin();

    // Call these to make the image change

    void requestUpdate(bool all = false); // Update display, if a foreground applet has info it wants to show
    void forceUpdate(Drivers::EInk::UpdateTypes type = Drivers::EInk::UpdateTypes::UNSPECIFIED, bool all = false,
                     bool async = true); // Update display, regardless of whether any applets requested this

    // Wait for an update to complete
    void awaitUpdate();

    // Receives pixel output from an applet (via a tile, which translates the coordinates)
    void handlePixel(int16_t x, int16_t y, Color c);

#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    // Fast path for writing MSB-first 1-bpp bitmaps in the same coordinate space as handlePixel().
    void writeBitmap(int16_t screenX, int16_t screenY, uint16_t w, uint16_t h, const uint8_t *bitmap, uint16_t bitmapStride,
                     uint16_t sourceX = 0, uint16_t sourceY = 0);
#if INKHUD_BOOKREADER_GRAYSCALE
    void writeGrayBitmap(int16_t screenX, int16_t screenY, uint16_t w, uint16_t h, const uint8_t *bitmap, uint16_t bitmapStride,
                         uint16_t sourceX = 0, uint16_t sourceY = 0);
#endif
#endif

    // Size of display, in context of current rotation

    uint16_t width();
    uint16_t height();

  private:
    // Make attempts to render / update, once triggered by requestUpdate or forceUpdate
    int32_t runOnce() override;

    // Apply the display rotation to handled pixels
    void rotatePixelCoords(int16_t *x, int16_t *y);

    // Execute the render process now, then hand off to driver for display update
    void render(bool async = true);
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    void maintainRenderActivity();
#endif

    // Steps of the rendering process

    void clearBuffer();
    void clearTile(Tile *t);
    void checkLocks();
    bool shouldUpdate();
    Drivers::EInk::UpdateTypes decideUpdateType();
    void renderUserApplets();
    void renderSystemApplets();
    void renderPlaceholders();

    Drivers::EInk *driver = nullptr; // Interacts with your variants display hardware
    DisplayHealth displayHealth;     // Manages display health by controlling type of update

    uint8_t *imageBuffer = nullptr; // Fed into driver
    uint16_t imageBufferHeight = 0;
    uint16_t imageBufferWidth = 0;
    uint32_t imageBufferSize = 0; // Bytes
#if INKHUD_BOOKREADER_GRAYSCALE
    uint8_t *grayBuffer = nullptr;
    uint32_t grayPixelCount = 0;
    bool monoBufferNeedsSync = false;
    void setGrayPixel(uint16_t x, uint16_t y, uint8_t luminance);
#endif

    SystemApplet *lockRendering = nullptr; // Render this applet *only*
    SystemApplet *lockRequests = nullptr;  // Honor update requests from this applet *only*

    bool requested = false;
    bool forced = false;
    bool renderAll = false;
#ifdef MESHTASTIC_INCLUDE_INKHUD_BOOKREADER
    bool keepAwakeUntilUpdateComplete = false;
#endif

    // For convenience
    InkHUD *inkhud = nullptr;
    Persistence::Settings *settings = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif
