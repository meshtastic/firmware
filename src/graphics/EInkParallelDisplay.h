#pragma once

#include "configuration.h"

#ifdef USE_EINK_PARALLELDISPLAY
#include <OLEDDisplay.h>

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
#ifndef EINK_FORCE_DISPLAY_THROTTLE_MS
#define EINK_FORCE_DISPLAY_THROTTLE_MS 200
#endif
#endif

class FASTEPD;

/**
 * Adapter for E-Ink 8-bit parallel displays (EPD), specifically devices supported by FastEPD library
 */
class EInkParallelDisplay : public OLEDDisplay
{
  public:
    enum EpdRotation {
        EPD_ROT_LANDSCAPE = 0,
        EPD_ROT_PORTRAIT = 90,
        EPD_ROT_INVERTED_LANDSCAPE = 180,
        EPD_ROT_INVERTED_PORTRAIT = 270,
    };

    EInkParallelDisplay(uint16_t width, uint16_t height, EpdRotation rotation);
#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    EInkParallelDisplay(uint16_t logicalWidth, uint16_t logicalHeight, uint16_t panelWidth, uint16_t panelHeight,
                        EpdRotation rotation);
#endif
    virtual ~EInkParallelDisplay();

    // OLEDDisplay virtuals
    bool connect() override;
    void sendCommand(uint8_t com) override;
    int getBufferOffset(void) override { return 0; }

    void display(void) override;
#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    bool forceDisplay(uint32_t msecLimit = EINK_FORCE_DISPLAY_THROTTLE_MS);
    void requestResponsiveUpdate() { responsiveUpdateRequested = true; }
    void requestFullRefresh()
    {
        fullRefreshRequested = true;
        responsiveUpdateRequested = true;
    }
#else
    bool forceDisplay(uint32_t msecLimit = 1000);
#endif
    void endUpdate();

  protected:
    uint32_t lastDrawMsec = 0;
    FASTEPD *epaper;

    // Set only when connect() fully succeeds; framebuffer-touching methods no-op while false.
    bool displayReady = false;

  private:
    // Async full-refresh support
    std::atomic<bool> asyncFullRunning{false};
    TaskHandle_t asyncTaskHandle = nullptr;
    void startAsyncFullUpdate(int clearMode);
    static void asyncFullUpdateTask(void *pvParameters);
#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    bool updateFrame(uint32_t msecLimit);
    void mapLogicalToPanel(uint16_t logicalX, uint16_t logicalY, uint16_t &panelX, uint16_t &panelY) const;
#endif

#ifdef EINK_LIMIT_GHOSTING_PX
    // helpers
    void resetGhostPixelTracking();
    void markDirtyBits(const uint8_t *prevBuf, uint32_t pos, uint8_t mask, uint8_t out);
    void countGhostPixelsAndMaybePromote(int &newTop, int &newBottom, bool &forceFull);

    // per-bit dirty buffer (same format as epaper buffers): one bit == one pixel
    uint8_t *dirtyPixels = nullptr;
    size_t dirtyPixelsSize = 0;
    uint32_t ghostPixelCount = 0;
    uint32_t ghostPixelLimit = EINK_LIMIT_GHOSTING_PX;
#endif

    EpdRotation rotation;
#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    uint16_t panelWidth;
    uint16_t panelHeight;
    uint16_t panelRowBytes;
    uint32_t panelBufferSize;
#endif
    uint32_t previousImageHash = 0;
#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    bool hasPresentedFrame = false;
    bool fullRefreshRequested = false;
    bool responsiveUpdateRequested = false;
#endif
    uint32_t lastUpdateMs = 0;
    int fastRefreshCount = 0;
};

#endif
