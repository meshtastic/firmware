#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_APPS

#if HAS_SCREEN

#include "modules/AppModule/AppDisplay.h"
#include "graphics/ScreenFonts.h"

// Current display state for the active frame
static OLEDDisplay *currentDisplay = nullptr;
static int16_t currentOffsetX = 0;
static int16_t currentOffsetY = 0;

void app_display_set(OLEDDisplay *display, int16_t x, int16_t y)
{
    currentDisplay = display;
    currentOffsetX = x;
    currentOffsetY = y;
}

void app_display_draw_string(int16_t x, int16_t y, const char *text)
{
    if (currentDisplay)
        currentDisplay->drawString(x + currentOffsetX, y + currentOffsetY, text);
}

void app_display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (currentDisplay)
        currentDisplay->drawLine(x0 + currentOffsetX, y0 + currentOffsetY, x1 + currentOffsetX, y1 + currentOffsetY);
}

void app_display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (currentDisplay)
        currentDisplay->drawRect(x + currentOffsetX, y + currentOffsetY, w, h);
}

void app_display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (currentDisplay)
        currentDisplay->fillRect(x + currentOffsetX, y + currentOffsetY, w, h);
}

void app_display_draw_circle(int16_t x, int16_t y, int16_t r)
{
    if (currentDisplay)
        currentDisplay->drawCircle(x + currentOffsetX, y + currentOffsetY, r);
}

void app_display_fill_circle(int16_t x, int16_t y, int16_t r)
{
    if (currentDisplay)
        currentDisplay->fillCircle(x + currentOffsetX, y + currentOffsetY, r);
}

void app_display_set_color(int c)
{
    if (currentDisplay)
        currentDisplay->setColor((OLEDDISPLAY_COLOR)c);
}

void app_display_set_font(const char *name)
{
    if (currentDisplay) {
        if (strcmp(name, "small") == 0) {
            currentDisplay->setFont(FONT_SMALL);
        } else if (strcmp(name, "medium") == 0) {
            currentDisplay->setFont(FONT_MEDIUM);
        } else if (strcmp(name, "large") == 0) {
            currentDisplay->setFont(FONT_LARGE);
        }
    }
}

void app_display_draw_string_wrapped(int16_t x, int16_t y, int16_t maxWidth, const char *text)
{
    if (currentDisplay)
        currentDisplay->drawStringMaxWidth(x + currentOffsetX, y + currentOffsetY, maxWidth, text);
}

int app_display_width()
{
    return currentDisplay ? currentDisplay->getWidth() : 0;
}

int app_display_height()
{
    return currentDisplay ? currentDisplay->getHeight() : 0;
}

#endif // HAS_SCREEN

#endif // !MESHTASTIC_EXCLUDE_APPS
