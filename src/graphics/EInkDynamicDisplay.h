#pragma once

#include "configuration.h"

#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)

#include "EInkDisplay2.h"
#include "GxEPD2_BW.h"
#include "concurrency/NotifiedWorkerThread.h"

/*
    Derives from the EInkDisplay adapter class.
    Accepts suggestions from Screen class about frame type.
    Determines which refresh type is most suitable.
    (Full, Fast, Skip)
*/

class EInkDynamicDisplay : public EInkDisplay, protected concurrency::NotifiedWorkerThread
{
  public:
    // Constructor
    // ( Parameters unused, passed to EInkDisplay. Maintains compatibility OLEDDisplay class )
    EInkDynamicDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus);
    ~EInkDynamicDisplay();

    // Methods to enable or disable unlimited fast refresh mode
    void enableUnlimitedFastMode() { addFrameFlag(UNLIMITED_FAST); }
    void disableUnlimitedFastMode() { frameFlags = (frameFlagTypes)(frameFlags & ~UNLIMITED_FAST); }

    // What kind of frame is this
    enum frameFlagTypes : uint8_t {
        BACKGROUND = (1 << 0),  // For frames via display()
        RESPONSIVE = (1 << 1),  // For frames via forceDisplay()
        COSMETIC = (1 << 2),    // For splashes
        DEMAND_FAST = (1 << 3), // Special case only
        BLOCKING = (1 << 4),    // Modifier - block while refresh runs
        UNLIMITED_FAST = (1 << 5)
    };
    void addFrameFlag(frameFlagTypes flag);

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
        ASYNC_REFRESH_BLOCKED_DEMANDFAST,
        ASYNC_REFRESH_BLOCKED_COSMETIC,
        ASYNC_REFRESH_BLOCKED_RESPONSIVE,
        ASYNC_REFRESH_BLOCKED_BACKGROUND,
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

    enum notificationTypes : uint8_t { // What was onNotify() called for
        NONE = 0,                      // This behavior (NONE=0) is fixed by NotifiedWorkerThread class
        DUE_POLL_ASYNCREFRESH = 1,
    };
    const uint32_t intervalPollAsyncRefresh = 100;

    void onNotify(uint32_t notification) override; // Handle any async tasks - overrides NotifiedWorkerThread
    void configForFastRefresh();                   // GxEPD2 code to set fast-refresh
    void configForFullRefresh();                   // GxEPD2 code to set full-refresh
    bool determineMode();                          // Assess situation, pick a refresh type
    void applyRefreshMode();                       // Run any relevant GxEPD2 code, so next update will use correct refresh type
    void adjustRefreshCounters();                  // Update fastRefreshCount
    bool update();                                 // Trigger the display update - determine mode, then call base class
    void endOrDetach();                            // Run the post-update code, or delegate it off to checkBusyAsyncRefresh()

    // Checks as part of determineMode()
    void checkInitialized();              // Is this the very first frame?
    void checkForPromotion();             // Was a frame skipped (rate, display busy) that should have been a FAST refresh?
    void checkRateLimiting();             // Is this frame too soon?
    void checkCosmetic();                 // Was the COSMETIC flag set?
    void checkDemandingFast();            // Was the DEMAND_FAST flag set?
    void checkFrameMatchesPrevious();     // Does the new frame match the existing display image?
    void checkConsecutiveFastRefreshes(); // Too many fast-refreshes consecutively?
    void checkFastRequested();            // Was the flag set for RESPONSIVE, or only BACKGROUND?

    void resetRateLimiting(); // Set previousRunMs - this now counts as an update, for rate-limiting
    void hashImage();         // Generate a hashed version of this frame, to compare against previous update
    void storeAndReset();     // Keep results of determineMode() for later, tidy-up for next call

    // What we are determining for this frame
    frameFlagTypes frameFlags = BACKGROUND; // Frame characteristics - determineMode() input
    refreshTypes refresh = UNSPECIFIED;     // Refresh type - determineMode() output
    reasonTypes reason = NO_OBJECTIONS;     // Reason - why was refresh type used

    // What happened last time determineMode() ran
    frameFlagTypes previousFrameFlags = BACKGROUND; // (Previous) Frame flags
    refreshTypes previousRefresh = UNSPECIFIED;     // (Previous) Outcome
    reasonTypes previousReason = NO_OBJECTIONS;     // (Previous) Reason

    bool initialized = false;          // Have we drawn at least one frame yet?
    uint32_t previousRunMs = -1;       // When did determineMode() last run (rather than rejecting for rate-limiting)
    uint32_t imageHash = 0;            // Hash of the current frame. Don't bother updating if nothing has changed!
    uint32_t previousImageHash = 0;    // Hash of the previous update's frame
    uint32_t fastRefreshCount = 0;     // How many fast-refreshes consecutively since last full refresh?
    refreshTypes currentConfig = FULL; // Which refresh type is GxEPD2 currently configured for

    // Optional - track ghosting, pixel by pixel
    // May 2024: no longer used by any display. Kept for possible future use.
#ifdef EINK_LIMIT_GHOSTING_PX
    void countGhostPixels();        // Count any pixels which have moved from black to white since last full-refresh
    void checkExcessiveGhosting();  // Check if ghosting exceeds defined limit
    void resetGhostPixelTracking(); // Clear the dirty pixels array. Call when full-refresh cleans the display.
    uint8_t *dirtyPixels;           // Any pixels that have been black since last full-refresh (dynamically allocated mem)
    uint32_t ghostPixelCount = 0;   // Number of pixels with problematic ghosting. Retained here for LOG_DEBUG use
#endif

    // Conditional - async full refresh - only with modified meshtastic/GxEPD2
#if defined(HAS_EINK_ASYNCFULL)
  public:
    void joinAsyncRefresh(); // Main thread joins an async refresh already in progress. Blocks, then runs post-update code

  protected:
    void pollAsyncRefresh();          // Run the post-update code if the hardware is ready
    void checkBusyAsyncRefresh();     // Check if display is busy running an async full-refresh (rejecting new frames)
    void awaitRefresh();              // Hold control while an async refresh runs
    void endUpdate() override {}      // Disable base-class behavior of running post-update immediately after forceDisplay()
    bool asyncRefreshRunning = false; // Flag, checked by checkBusyAsyncRefresh()
#else
  public:
    void joinAsyncRefresh() {} // Dummy method

  protected:
    void pollAsyncRefresh() {} // Dummy method. In theory, not reachable
#endif
};

// Hide the ugly casts used in Screen.cpp
#define EINK_ADD_FRAMEFLAG(display, flag) static_cast<EInkDynamicDisplay *>(display)->addFrameFlag(EInkDynamicDisplay::flag)
#define EINK_JOIN_ASYNCREFRESH(display) static_cast<EInkDynamicDisplay *>(display)->joinAsyncRefresh()

#else // !USE_EINK_DYNAMICDISPLAY
// Dummy-macro, removes the need for include guards
#define EINK_ADD_FRAMEFLAG(display, flag)
#define EINK_JOIN_ASYNCREFRESH(display)
#endif