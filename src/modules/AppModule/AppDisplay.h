#pragma once

#if HAS_SCREEN

#include <OLEDDisplay.h>

// Set the current display pointer and offset for the frame being drawn
void app_display_set(OLEDDisplay *display, int16_t x, int16_t y);

// Display drawing functions exposed to app bindings
void app_display_draw_string(int16_t x, int16_t y, const char *text);
void app_display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void app_display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h);
void app_display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h);
void app_display_draw_circle(int16_t x, int16_t y, int16_t r);
void app_display_fill_circle(int16_t x, int16_t y, int16_t r);
void app_display_set_color(int c);
void app_display_set_font(const char *name);
void app_display_draw_string_wrapped(int16_t x, int16_t y, int16_t maxWidth, const char *text);
int app_display_width();
int app_display_height();

#endif // HAS_SCREEN
