#pragma once

#ifdef OLED_PL
#include "graphics/fonts/OLEDDisplayFontsPL.h"
#endif

#ifdef OLED_RU
#include "graphics/fonts/OLEDDisplayFontsRU.h"
#endif

#ifdef OLED_UA
#include "graphics/fonts/OLEDDisplayFontsUA.h"
#endif

#ifdef OLED_CS
#include "graphics/fonts/OLEDDisplayFontsCS.h"
#endif

#ifdef OLED_GR
#include "graphics/fonts/OLEDDisplayFontsGR.h"
#endif

#if (defined(CROWPANEL_ESP32S3_5_EPAPER) || defined(T5_S3_EPAPER_PRO)) && defined(USE_EINK)
#include "graphics/fonts/EinkDisplayFonts.h"
#endif

#ifdef OLED_GR
#define FONT_SMALL_LOCAL ArialMT_Plain_10_GR // Height: 13
#else
#ifdef OLED_PL
#define FONT_SMALL_LOCAL ArialMT_Plain_10_PL
#else
#ifdef OLED_RU
#define FONT_SMALL_LOCAL ArialMT_Plain_10_RU
#else
#ifdef OLED_UA
#define FONT_SMALL_LOCAL ArialMT_Plain_10_UA // Height: 13
#else
#ifdef OLED_CS
#define FONT_SMALL_LOCAL ArialMT_Plain_10_CS
#else
#define FONT_SMALL_LOCAL ArialMT_Plain_10 // Height: 13
#endif
#endif
#endif
#endif
#endif
#ifdef OLED_GR
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16_GR // Height: 19
#else
#ifdef OLED_PL
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16_PL // Height: 19
#else
#ifdef OLED_RU
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16_RU // Height: 19
#else
#ifdef OLED_UA
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16_UA // Height: 19
#else
#ifdef OLED_CS
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16_CS
#else
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16 // Height: 19
#endif
#endif
#endif
#endif
#endif
#ifdef OLED_GR
#define FONT_LARGE_LOCAL ArialMT_Plain_24_GR // Height: 28
#else
#ifdef OLED_PL
#define FONT_LARGE_LOCAL ArialMT_Plain_24_PL // Height: 28
#else
#ifdef OLED_RU
#define FONT_LARGE_LOCAL ArialMT_Plain_24_RU // Height: 28
#else
#ifdef OLED_UA
#define FONT_LARGE_LOCAL ArialMT_Plain_24_UA // Height: 28
#else
#ifdef OLED_CS
#define FONT_LARGE_LOCAL ArialMT_Plain_24_CS // Height: 28
#else
#define FONT_LARGE_LOCAL ArialMT_Plain_24 // Height: 28
#endif
#endif
#endif
#endif
#endif

// ---------------------------------------------------------------------------
// Flash budget: nRF52 boards drop the 24pt glyph (~9.6 KB) and substitute the
// 16pt one. Other architectures (ESP32, RP2040, Portduino, STM32) always keep
// 24pt. Any nRF52 variant that wants 24pt back can set
// MESHTASTIC_LARGE_FONT_24PT=1 in its build flags.
// ---------------------------------------------------------------------------
#if defined(ARCH_NRF52) && !defined(MESHTASTIC_LARGE_FONT_24PT)
#define MESHTASTIC_DROP_24PT_FONT
#endif

// ---------------------------------------------------------------------------
// Display tier → pick FONT_SMALL/MEDIUM/LARGE.
//   BIG     — eInk panel / TFT / Hackaday Communicator.
//   TINY    — M5STACK_UNITC6L only.
//   default — 128x64 SSD1306 / SH1106 small OLED.
// DISPLAY_FORCE_SMALL_FONTS opts a big-screen variant out of BIG (rarely used).
// ---------------------------------------------------------------------------
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(HX8357_CS) || defined(ILI9488_CS) || defined(ST7796_CS) ||             \
     defined(USE_ST7796) || defined(HACKADAY_COMMUNICATOR)) &&                                                                   \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
// Tier BIG. SMALL is 16pt; MEDIUM/LARGE normally 24pt.
#define FONT_SMALL FONT_MEDIUM_LOCAL // 16pt
#if defined(MESHTASTIC_DROP_24PT_FONT) && defined(USE_EINK)
// Flash-tight nRF52 eInk: collapse MEDIUM/LARGE to 16pt too.
#define FONT_MEDIUM FONT_MEDIUM_LOCAL // 16pt
#define FONT_LARGE FONT_MEDIUM_LOCAL  // 16pt
#else
#define FONT_MEDIUM FONT_LARGE_LOCAL // 24pt
#define FONT_LARGE FONT_LARGE_LOCAL  // 24pt
#endif
#elif defined(M5STACK_UNITC6L)
// Tier TINY — 10pt everywhere.
#define FONT_SMALL FONT_SMALL_LOCAL  // 10pt
#define FONT_MEDIUM FONT_SMALL_LOCAL // 10pt
#define FONT_LARGE FONT_SMALL_LOCAL  // 10pt
#else
// Default tier — small OLED.
#define FONT_SMALL FONT_SMALL_LOCAL   // 10pt
#define FONT_MEDIUM FONT_MEDIUM_LOCAL // 16pt
#if defined(MESHTASTIC_DROP_24PT_FONT)
// Flash-tight nRF52 small-OLED: substitute 16pt for 24pt. Only the BLE PIN
// screen and one audio-module screen use FONT_LARGE on this tier.
#define FONT_LARGE FONT_MEDIUM_LOCAL // 16pt
#else
#define FONT_LARGE FONT_LARGE_LOCAL // 24pt
#endif
#endif

// CrowPanel-S3 / T5-S3 ePaper override everything with their own 30pt font.
#if defined(CROWPANEL_ESP32S3_5_EPAPER) || defined(T5_S3_EPAPER_PRO)
#undef FONT_SMALL
#undef FONT_MEDIUM
#undef FONT_LARGE
#define FONT_SMALL Monospaced_plain_30
#define FONT_MEDIUM Monospaced_plain_30
#define FONT_LARGE Monospaced_plain_30
#endif

#define _fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL _fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM _fontHeight(FONT_MEDIUM)
#define FONT_HEIGHT_LARGE _fontHeight(FONT_LARGE)
