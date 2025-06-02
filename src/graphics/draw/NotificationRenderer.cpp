#include "NotificationRenderer.h"
#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include <algorithm>
#include <string>
#include <vector>

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#endif

using namespace meshtastic;

// External references to global variables from Screen.cpp
extern std::vector<std::string> functionSymbol;
extern std::string functionSymbolString;
extern bool hasUnreadMessage;

namespace graphics
{

namespace NotificationRenderer
{

// Used on boot when a certificate is being created
void NotificationRenderer::drawSSLScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    display->drawString(64 + x, y, "Creating SSL certificate");

#ifdef ARCH_ESP32
    yield();
    esp_task_wdt_reset();
#endif

    display->setFont(FONT_SMALL);
    if ((millis() / 1000) % 2) {
        display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait . . .");
    } else {
        display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Please wait . .  ");
    }
}

// Used when booting without a region set
void NotificationRenderer::drawWelcomeScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(64 + x, y, "//\\ E S H T /\\ S T / C");
    display->drawString(64 + x, y + FONT_HEIGHT_SMALL, getDeviceName());
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if ((millis() / 10000) % 2) {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 2 - 3, "Set the region using the");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 3 - 3, "Meshtastic Android, iOS,");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 4 - 3, "Web or CLI clients.");
    } else {
        display->drawString(x, y + FONT_HEIGHT_SMALL * 2 - 3, "Visit meshtastic.org");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 3 - 3, "for more information.");
        display->drawString(x, y + FONT_HEIGHT_SMALL * 4 - 3, "");
    }

#ifdef ARCH_ESP32
    yield();
    esp_task_wdt_reset();
#endif
}

void NotificationRenderer::drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // Exit if no message is active or duration has passed
    if (strlen(screen->alertBannerMessage) == 0 || (screen->alertBannerUntil != 0 && millis() > screen->alertBannerUntil))
        return;

    // === Layout Configuration ===
    constexpr uint16_t padding = 5;    // Padding around text inside the box
    constexpr uint8_t lineSpacing = 1; // Extra space between lines

    // Search the message to determine if we need the bell added
    bool needs_bell = (strstr(screen->alertBannerMessage, "Alert Received") != nullptr);

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT); // We will manually center per line
    const int MAX_LINES = 10;

    uint16_t maxWidth = 0;
    uint16_t lineWidths[MAX_LINES] = {0};
    uint16_t lineLengths[MAX_LINES] = {0};
    char *lineStarts[MAX_LINES + 1];
    uint16_t lineCount = 0;
    char lineBuffer[40] = {0};
    // pointer to the terminating null
    char *alertEnd = screen->alertBannerMessage + strnlen(screen->alertBannerMessage, sizeof(screen->alertBannerMessage));
    lineStarts[lineCount] = screen->alertBannerMessage;
    LOG_WARN(lineStarts[lineCount]);

    // loop through lines finding \n characters
    while ((lineCount < 10) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find(lineStarts[lineCount], alertEnd, '\n') + 1;
        lineLengths[lineCount] = lineStarts[lineCount + 1] - lineStarts[lineCount];
        lineWidths[lineCount] = display->getStringWidth(lineStarts[lineCount], lineLengths[lineCount], true);
        if (lineWidths[lineCount] > maxWidth) {
            maxWidth = lineWidths[lineCount];
        }
        lineCount++;
    }

    // set width from longest line
    uint16_t boxWidth = padding * 2 + maxWidth;
    if (needs_bell) {
        if (SCREEN_WIDTH > 128 && boxWidth < 106) {
            boxWidth += 26;
        }
        if (SCREEN_WIDTH <= 128 && boxWidth < 78) {
            boxWidth += 20;
        }
    }

    // set height from line count
    uint16_t boxHeight = padding * 2 + lineCount * FONT_HEIGHT_SMALL + (lineCount - 1) * lineSpacing;

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);
    // === Draw background box ===
    display->setColor(BLACK);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Slightly oversized box
    display->setColor(WHITE);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight); // Border

    // === Draw each line centered in the box ===
    int16_t lineY = boxTop + padding;
    for (int i = 0; i < lineCount; i++) {
        strncpy(lineBuffer, lineStarts[i], 40);
        if (lineLengths[i] > 39)
            lineBuffer[39] = '\0';
        else
            lineBuffer[lineLengths[i]] = '\0';

        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;

        if (needs_bell && i == 0) {
            int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + lineWidths[i] + 2, bellY, 8, 8, bell_alert);
        }

        display->drawString(textX, lineY, lineBuffer);
        if (SCREEN_WIDTH > 128)
            display->drawString(textX + 1, lineY, lineBuffer); // Faux bold

        lineY += FONT_HEIGHT_SMALL + lineSpacing;
    }
}

/// Draw the last text message we received
void NotificationRenderer::drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);

    char tempBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "Critical fault #%d", error_code);
    display->drawString(0 + x, 0 + y, tempBuf);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(0 + x, FONT_HEIGHT_MEDIUM + y, "For help, please visit \nmeshtastic.org");
}

void NotificationRenderer::drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, y, "Updating");

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawStringMaxWidth(0 + x, 2 + y + FONT_HEIGHT_SMALL * 2, x + display->getWidth(),
                                "Please be patient and do not power off.");
}

} // namespace NotificationRenderer

} // namespace graphics
