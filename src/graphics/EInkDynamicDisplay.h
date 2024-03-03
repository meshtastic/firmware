#pragma once

#include "configuration.h"

#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)

#include "EInkDisplay2.h"
#include "GxEPD2_BW.h"

/*
    Derives from the EInkDisplay adapter class.
    Accepts suggestions from Screen class about frame type.
    Determines which refresh type is most suitable.
    (Full, Fast, Skip)
*/

class EInkDynamicDisplay : public EInkDisplay
{
  public:
    // Constructor
    // ( Parameters unused, passed to EInkDisplay. Maintains compatibility OLEDDisplay class )
    EInkDynamicDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus);
    ~EInkDynamicDisplay();

    // What kind of frame is this
    enum frameFlagTypes : uint8_t {
        BACKGROUND = (1 << 0),  // For frames via display()
        RESPONSIVE = (1 << 1),  // For frames via forceDisplay()
        COSMETIC = (1 << 2),    // For splashes
        DEMAND_FAST = (1 << 3), // Special case only
    };
    void setFrameFlag(frameFlagTypes flag);

    // Set the correct frame flag, then call universal "update()" method
    void display() override;
    bool forceDisplay(uint32_t msecLimit) override; // Shadows base class. Parameter and return val unused.

  protected:
    enum refreshTypes : uint8_t { // Which refresh operation will be used
        UNSPECIFIED,
        FULL,
        FAST,
        SKIPPED,
    };
    enum reasonTypes : uint8_t { // How was the decision reached
        NO_OBJECTIONS,
        EXCEEDED_RATELIMIT_FAST,
        EXCEEDED_RATELIMIT_FULL,
        FLAGGED_COSMETIC,
        FLAGGED_DEMAND_FAST,
        EXCEEDED_LIMIT_FASTREFRESH,
        EXCEEDED_GHOSTINGLIMIT,
        FRAME_MATCHED_PREVIOUS,
        BACKGROUND_USES_FAST,
        FLAGGED_BACKGROUND,
        REDRAW_WITH_FULL,
    };

    void configForFastRefresh();  // GxEPD2 code to set fast-refresh
    void configForFullRefresh();  // GxEPD2 code to set full-refresh
    bool determineMode();         // Assess situation, pick a refresh type
    void applyRefreshMode();      // Run any relevant GxEPD2 code, so next update will use correct refresh type
    void adjustRefreshCounters(); // Update fastRefreshCount
    bool update();                // Trigger the display update - determine mode, then call base class

    // Checks as part of determineMode()
    void checkWasFlooded();               // Was the previous frame skipped for exceeding EINK_LIMIT_RATE_RESPONSIVE_SEC?
    void checkRateLimiting();             // Is this frame too soon?
    void checkCosmetic();                 // Was the COSMETIC flag set?
    void checkDemandingFast();            // Was the DEMAND_FAST flag set?
    void checkConsecutiveFastRefreshes(); // Too many fast-refreshes consecutively?
    void checkFrameMatchesPrevious();     // Does the new frame match the existing display image?
    void checkFastRequested();            // Was the flag set for RESPONSIVE, or only BACKGROUND?

    void resetRateLimiting(); // Set previousRunMs - this now counts as an update, for rate-limiting
    void hashImage();         // Generate a hashed version of this frame, to compare against previous update
    void storeAndReset();     // Keep results of determineMode() for later, tidy-up for next call

    // What we are determining for this frame
    frameFlagTypes frameFlags = BACKGROUND; // Frame type(s) - determineMode() input
    refreshTypes refresh = UNSPECIFIED;     // Refresh type - determineMode() output
    reasonTypes reason = NO_OBJECTIONS;     // Reason - why was refresh type used

    // What happened last time determineMode() ran
    refreshTypes previousRefresh = UNSPECIFIED; // (Previous) Outcome
    reasonTypes previousReason = NO_OBJECTIONS; // (Previous) Reason

    uint32_t previousRunMs = -1;       // When did determineMode() last run (rather than rejecting for rate-limiting)
    uint32_t imageHash = 0;            // Hash of the current frame. Don't bother updating if nothing has changed!
    uint32_t previousImageHash = 0;    // Hash of the previous update's frame
    uint32_t fastRefreshCount = 0;     // How many fast-refreshes consecutively since last full refresh?
    refreshTypes currentConfig = FULL; // Which refresh type is GxEPD2 currently configured for

    // Optional - track ghosting, pixel by pixel
#ifdef EINK_LIMIT_GHOSTING_PX
    void countGhostPixels();        // Count any pixels which have moved from black to white since last full-refresh
    void checkExcessiveGhosting();  // Check if ghosting exceeds defined limit
    void resetGhostPixelTracking(); // Clear the dirty pixels array. Call when full-refresh cleans the display.
    uint8_t *dirtyPixels;           // Any pixels that have been black since last full-refresh (dynamically allocated mem)
    uint32_t ghostPixelCount = 0;   // Number of pixels with problematic ghosting. Retained here for LOG_DEBUG use
#endif
};

#endif