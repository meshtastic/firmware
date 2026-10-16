// =============================================================================
// config/display.h — E-Ink Display Configuration
// =============================================================================
// Panel model selection, orientation, and dynamic refresh tuning.
//
// NOTE: USE_EINK and USE_EINK_DYNAMICDISPLAY must be -D build flags in
// platformio.ini (the external GxEPD2 library uses them but never includes
// configuration.h).  All other EINK_* tuning macros are safe here.
// =============================================================================

#pragma once

// ---------------------------------------------------------------------------
// Panel model
// GxEPD2_213_B74: driver specifically for GDEM0213B74 (SSD1680).
// Uses the OTP built-in LUT for partial updates — correct for this panel.
// Physical: WIDTH=128 gate lines, HEIGHT=250 source lines.
// After setRotation(3): display coordinate width=250, height=128.
// ---------------------------------------------------------------------------
#define EINK_DISPLAY_MODEL GxEPD2_213_B74
#define EINK_WIDTH 250  // display width  after rotation-3 (source lines)
#define EINK_HEIGHT 122 // display height after rotation-3 (visible gate lines)

#define EINK_ROTATION 3

// -----------------------------------------------------------------------------
// Dynamic refresh control
// -----------------------------------------------------------------------------
#define EINK_BACKGROUND_USES_FAST
#define EINK_HASQUIRK_GHOSTING
#define EINK_LIMIT_FASTREFRESH 10
