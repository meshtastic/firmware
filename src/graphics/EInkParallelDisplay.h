#pragma once

#include "configuration.h"

#ifdef USE_EPD
#include <OLEDDisplay.h>

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class FASTEPD;

/**
 * Adapter for E-Ink 8-bit parallel displays (EPD), specifically devices supported by FastEPD library
 */
class EInkParallelDisplay : public OLEDDisplay
{
  public:
    enum EpdRotation {
        EPD_ROT_LANDSCAPE = 0,
        EPD_ROT_PORTRAIT = 1,
        EPD_ROT_INVERTED_LANDSCAPE = 2,
        EPD_ROT_INVERTED_PORTRAIT = 3,
    };

    EInkParallelDisplay(uint16_t width, uint16_t height, EpdRotation rotation);
    virtual ~EInkParallelDisplay();

    // OLEDDisplay virtuals
    bool connect() override;
    void sendCommand(uint8_t com) override;
    int getBufferOffset(void) override { return 0; }

    void display(void) override;
    bool forceDisplay(uint32_t msecLimit = 1000);
    void endUpdate();

  protected:
    uint32_t lastDrawMsec = 0;
    FASTEPD *epaper;

  private:
    // Async full-refresh support
    std::atomic<bool> asyncFullRunning{false};
    TaskHandle_t asyncTaskHandle = nullptr;
    void startAsyncFullUpdate(int clearMode);
    static void asyncFullUpdateTask(void *pvParameters);

    // helpers
#ifdef EINK_LIMIT_GHOSTING_PX
    void resetGhostPixelTracking();
    void markDirtyBits(const uint8_t *prevBuf, uint32_t pos, uint8_t mask, uint8_t out);
    void countGhostPixelsAndMaybePromote(int &newTop, int &newBottom, bool &forceFull);
#endif

    uint32_t previousImageHash = 0;
    uint32_t lastUpdateMs = 0;
    int fastRefreshCount = 0;
    // simple rate-limit (ms) for responsive updates
    static constexpr uint32_t RESPONSIVE_MIN_MS = 1000;
    // force a slow full update every N full updates
    static constexpr int FULL_SLOW_PERIOD = 50;

#ifdef EINK_LIMIT_GHOSTING_PX
    // per-bit dirty buffer (same format as epaper buffers): one bit == one pixel
    uint8_t *dirtyPixels = nullptr;
    size_t dirtyPixelsSize = 0;
    uint32_t ghostPixelCount = 0;
    uint32_t ghostPixelLimit = EINK_LIMIT_GHOSTING_PX;
#endif
};

#endif
