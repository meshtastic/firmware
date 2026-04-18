/*

OLEDDisplay adapter that routes BaseUI pixel output to a NicheGraphics::Drivers::EInk driver.

One adapter serves all E-Ink variants: the panel driver and orientation are injected at construction,
and FULL/FAST selection is made by the shared DisplayHealth model (same as InkHUD).

Replaces the per-board branching in EInkDisplay2 / EInkDynamicDisplay / EInkParallelDisplay.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "graphics/eink/Drivers/EInk.h"

#include <OLEDDisplay.h>

namespace NicheGraphics
{

class BaseUIEInkDisplay : public OLEDDisplay
{
  public:
    // Flags Screen.cpp sets via EINK_ADD_FRAMEFLAG before triggering a draw.
    // Bits are combined; decided at render time.
    enum frameFlagTypes : uint8_t {
        BACKGROUND = (1 << 0),     // Regular OLEDDisplayUi tick - no urgency, UNSPECIFIED
        RESPONSIVE = (1 << 1),     // User-driven refresh - prefer FAST
        COSMETIC = (1 << 2),       // Clean splash / wake-from-sleep - force FULL
        DEMAND_FAST = (1 << 3),    // Menu interaction - force FAST
        BLOCKING = (1 << 4),       // Wait for update to finish before returning
        UNLIMITED_FAST = (1 << 5), // Suppress health-driven FULL promotion (typing modes)
    };

    BaseUIEInkDisplay(Drivers::EInk *driver, uint8_t rotation);
    ~BaseUIEInkDisplay() override;

    // OLEDDisplay overrides
    bool connect() override;
    void display() override;
    void sendCommand(uint8_t com) override { (void)com; }
    int getBufferOffset(void) override { return 0; }

    // BaseUI public API (same shape as the old EInkDynamicDisplay)
    bool forceDisplay(uint32_t msecLimit = 1000);
    void addFrameFlag(frameFlagTypes flag);
    void joinAsyncRefresh();
    void enableUnlimitedFastMode() { addFrameFlag(UNLIMITED_FAST); }
    void disableUnlimitedFastMode() { frameFlags = (frameFlagTypes)(frameFlags & ~UNLIMITED_FAST); }

    // Tuning, called once per panel profile
    void setDisplayResilience(uint8_t fastPerFull, float stressMultiplier = 2.0f);

    // Exposed so Screen.cpp / variants can read the rotation passed in at construction
    uint8_t getRotation() const { return rotation; }

  private:
    // Perform an update now, unconditionally. Returns true if a frame was pushed to the driver.
    bool commit(Drivers::EInk::UpdateTypes type, bool blocking);

    // Convert OLEDDisplay's column-major buffer into the panel's row-major MSB-left buffer.
    // Applies rotation. Returns the hash of the panel buffer for frame-skip comparison.
    uint32_t repack();

    // Decide FULL vs FAST based on current frame flags + accumulated debt.
    Drivers::EInk::UpdateTypes decide();

    Drivers::EInk *driver = nullptr;
    uint8_t rotation = 0; // 0=0°, 1=90°CW, 2=180°, 3=270°CW
    uint8_t *panelBuffer = nullptr;
    uint32_t panelBufferSize = 0;
    uint16_t panelRowBytes = 0;

    frameFlagTypes frameFlags = BACKGROUND;
    uint32_t lastDrawMsec = 0;
    uint32_t lastHash = 0;

    // DisplayHealth-style debt tracking
    float fullRefreshDebt = 0.0f;
    uint8_t fastPerFull = 7;
    float stressMultiplier = 2.0f;
};

} // namespace NicheGraphics

// Compat macros used throughout Screen.cpp - route straight to the adapter.
#define EINK_ADD_FRAMEFLAG(display, flag)                                                                                        \
    static_cast<NicheGraphics::BaseUIEInkDisplay *>(display)->addFrameFlag(NicheGraphics::BaseUIEInkDisplay::flag)
#define EINK_JOIN_ASYNCREFRESH(display) static_cast<NicheGraphics::BaseUIEInkDisplay *>(display)->joinAsyncRefresh()

#else // !MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#define EINK_ADD_FRAMEFLAG(display, flag)
#define EINK_JOIN_ASYNCREFRESH(display)
#endif
