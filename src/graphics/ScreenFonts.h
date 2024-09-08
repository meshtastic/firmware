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

#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) || defined(ST7789_CS) ||           \
     defined(USE_ST7789) || defined(HX8357_CS)) &&                                                                               \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
// The screen is bigger so use bigger fonts
#define FONT_SMALL ArialMT_Plain_16  // Height: 19
#define FONT_MEDIUM ArialMT_Plain_24 // Height: 28
#define FONT_LARGE ArialMT_Plain_24  // Height: 28
#else
#ifdef OLED_PL
#define FONT_SMALL ArialMT_Plain_10_PL
#else
#ifdef OLED_RU
#define FONT_SMALL ArialMT_Plain_10_RU
#else
#ifdef OLED_UA
#define FONT_SMALL ArialMT_Plain_10_UA
#else
#define FONT_SMALL ArialMT_Plain_10 // Height: 13
#endif
#endif
#endif
#define FONT_MEDIUM ArialMT_Plain_16 // Height: 19
#define FONT_LARGE ArialMT_Plain_24  // Height: 28
#endif

#define _fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL _fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM _fontHeight(FONT_MEDIUM)
#define FONT_HEIGHT_LARGE _fontHeight(FONT_LARGE)