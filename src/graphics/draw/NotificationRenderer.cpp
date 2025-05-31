#include "NotificationRenderer.h"
#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "configuration.h"
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
extern String alertBannerMessage;
extern uint32_t alertBannerUntil;
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
    if (alertBannerMessage.length() == 0 || (alertBannerUntil != 0 && millis() > alertBannerUntil))
        return;

    // === Layout Configuration ===
    constexpr uint16_t padding = 5;    // Padding around text inside the box
    constexpr uint8_t lineSpacing = 1; // Extra space between lines

    // Search the message to determine if we need the bell added
    bool needs_bell = (alertBannerMessage.indexOf("Alert Received") != -1);

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT); // We will manually center per line

    // === Split the message into lines (supports multi-line banners) ===
    std::vector<String> lines;
    int start = 0, newlineIdx;
    while ((newlineIdx = alertBannerMessage.indexOf('\n', start)) != -1) {
        lines.push_back(alertBannerMessage.substring(start, newlineIdx));
        start = newlineIdx + 1;
    }
    lines.push_back(alertBannerMessage.substring(start));

    // === Measure text dimensions ===
    uint16_t minWidth = (SCREEN_WIDTH > 128) ? 106 : 78;
    uint16_t maxWidth = 0;
    std::vector<uint16_t> lineWidths;
    for (const auto &line : lines) {
        uint16_t w = display->getStringWidth(line.c_str(), line.length(), true);
        lineWidths.push_back(w);
        if (w > maxWidth)
            maxWidth = w;
    }

    uint16_t boxWidth = padding * 2 + maxWidth;
    if (needs_bell && boxWidth < minWidth)
        boxWidth += (SCREEN_WIDTH > 128) ? 26 : 20;

    uint16_t boxHeight = padding * 2 + lines.size() * FONT_HEIGHT_SMALL + (lines.size() - 1) * lineSpacing;

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);

    // === Draw background box ===
    display->setColor(BLACK);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Slightly oversized box
    display->setColor(WHITE);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight); // Border

    // === Draw each line centered in the box ===
    int16_t lineY = boxTop + padding;
    for (size_t i = 0; i < lines.size(); ++i) {
        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;
        uint16_t line_width = display->getStringWidth(lines[i].c_str(), lines[i].length(), true);

        if (needs_bell && i == 0) {
            int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + line_width + 2, bellY, 8, 8, bell_alert);
        }

        display->drawString(textX, lineY, lines[i]);
        if (SCREEN_WIDTH > 128)
            display->drawString(textX + 1, lineY, lines[i]); // Faux bold

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
