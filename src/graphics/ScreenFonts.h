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

#ifdef CROWPANEL_ESP32S3_5_EPAPER
#include "graphics/fonts/EinkDisplayFonts.h"
#endif

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
#ifdef OLED_PL
#define FONT_MEDIUM_LOCAL ArialMT_Plain_16_PL // Height: 19
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
#ifdef OLED_PL
#define FONT_LARGE_LOCAL ArialMT_Plain_24_PL // Height: 28
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

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(HX8357_CS)) ||                                                         \
    defined(ILI9488_CS) && !defined(DISPLAY_FORCE_SMALL_FONTS)
// The screen is bigger so use bigger fonts
#define FONT_SMALL FONT_MEDIUM_LOCAL // Height: 19
#define FONT_MEDIUM FONT_LARGE_LOCAL // Height: 28
#define FONT_LARGE FONT_LARGE_LOCAL  // Height: 28
#else
#define FONT_SMALL FONT_SMALL_LOCAL   // Height: 13
#define FONT_MEDIUM FONT_MEDIUM_LOCAL // Height: 19
#define FONT_LARGE FONT_LARGE_LOCAL   // Height: 28
#endif

#if defined(CROWPANEL_ESP32S3_5_EPAPER)
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
