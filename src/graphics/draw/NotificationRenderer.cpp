#include "configuration.h"
#if HAS_SCREEN

#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "NotificationRenderer.h"
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

char NotificationRenderer::inEvent = INPUT_BROKER_NONE;
int8_t NotificationRenderer::curSelected = 0;
char NotificationRenderer::alertBannerMessage[256] = {0};
uint32_t NotificationRenderer::alertBannerUntil = 0;  // 0 is a special case meaning forever
uint8_t NotificationRenderer::alertBannerOptions = 0; // last x lines are seelctable options
const char **NotificationRenderer::optionsArrayPtr = nullptr;
std::function<void(int)> NotificationRenderer::alertBannerCallback = NULL;
bool NotificationRenderer::pauseBanner = false;

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

void NotificationRenderer::drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    if (!isOverlayBannerShowing() || pauseBanner)
        return;

    // === Layout Configuration ===
    constexpr uint16_t hPadding = 5;
    constexpr uint16_t vPadding = 2;
    constexpr uint8_t lineSpacing = 1;

    bool needs_bell = (strstr(alertBannerMessage, "Alert Received") != nullptr);

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    constexpr int MAX_LINES = 5;
    uint16_t optionWidths[alertBannerOptions] = {0};
    uint16_t maxWidth = 0;
    uint16_t arrowsWidth = display->getStringWidth(">  <", 4, true);
    uint16_t lineWidths[MAX_LINES] = {0};
    uint16_t lineLengths[MAX_LINES] = {0};
    char *lineStarts[MAX_LINES + 1];
    uint16_t lineCount = 0;
    char lineBuffer[40] = {0};

    // Parse lines
    char *alertEnd = alertBannerMessage + strnlen(alertBannerMessage, sizeof(alertBannerMessage));
    lineStarts[lineCount] = alertBannerMessage;

    while ((lineCount < MAX_LINES) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find(lineStarts[lineCount], alertEnd, '\n');
        lineLengths[lineCount] = lineStarts[lineCount + 1] - lineStarts[lineCount];
        if (lineStarts[lineCount + 1][0] == '\n')
            lineStarts[lineCount + 1] += 1;
        lineWidths[lineCount] = display->getStringWidth(lineStarts[lineCount], lineLengths[lineCount], true);
        if (lineWidths[lineCount] > maxWidth)
            maxWidth = lineWidths[lineCount];
        lineCount++;
    }

    // Measure option widths
    for (int i = 0; i < alertBannerOptions; i++) {
        optionWidths[i] = display->getStringWidth(optionsArrayPtr[i], strlen(optionsArrayPtr[i]), true);
        if (optionWidths[i] > maxWidth)
            maxWidth = optionWidths[i];
        if (optionWidths[i] + arrowsWidth > maxWidth)
            maxWidth = optionWidths[i] + arrowsWidth;
    }

    // Handle input
    if (alertBannerOptions > 0) {
        if (inEvent == INPUT_BROKER_UP || inEvent == INPUT_BROKER_ALT_PRESS) {
            curSelected--;
        } else if (inEvent == INPUT_BROKER_DOWN || inEvent == INPUT_BROKER_USER_PRESS) {
            curSelected++;
        } else if (inEvent == INPUT_BROKER_SELECT) {
            alertBannerCallback(curSelected);
            alertBannerMessage[0] = '\0';
        } else if ((inEvent == INPUT_BROKER_CANCEL || inEvent == INPUT_BROKER_ALT_LONG) && alertBannerUntil != 0) {
            alertBannerMessage[0] = '\0';
        }

        if (curSelected == -1)
            curSelected = alertBannerOptions - 1;
        if (curSelected == alertBannerOptions)
            curSelected = 0;
    } else {
        if (inEvent == INPUT_BROKER_SELECT || inEvent == INPUT_BROKER_ALT_LONG || inEvent == INPUT_BROKER_CANCEL) {
            alertBannerMessage[0] = '\0';
        }
    }

    inEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    // === Box Size Calculation ===
    uint16_t boxWidth = hPadding * 2 + maxWidth;
    if (needs_bell) {
        if (isHighResolution && boxWidth <= 150)
            boxWidth += 26;
        if (!isHighResolution && boxWidth <= 100)
            boxWidth += 20;
    }

    uint16_t totalLines = lineCount + alertBannerOptions;
    uint16_t screenHeight = display->height();
    uint8_t effectiveLineHeight = FONT_HEIGHT_SMALL - 3;
    uint8_t visibleTotalLines = std::min<uint8_t>(totalLines, (screenHeight - vPadding * 2) / effectiveLineHeight);
    uint16_t contentHeight = visibleTotalLines * effectiveLineHeight;
    uint16_t boxHeight = contentHeight + vPadding * 2;

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);

    // === Draw Box ===
    display->setColor(BLACK);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2);
    display->fillRect(boxLeft, boxTop - 2, boxWidth, 1);
    display->fillRect(boxLeft, boxTop + boxHeight + 1, boxWidth, 1);
    display->fillRect(boxLeft - 2, boxTop, 1, boxHeight);
    display->fillRect(boxLeft + boxWidth + 1, boxTop, 1, boxHeight);
    display->setColor(WHITE);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);
    display->setColor(BLACK);
    display->fillRect(boxLeft, boxTop, 1, 1);
    display->fillRect(boxLeft + boxWidth - 1, boxTop, 1, 1);
    display->fillRect(boxLeft, boxTop + boxHeight - 1, 1, 1);
    display->fillRect(boxLeft + boxWidth - 1, boxTop + boxHeight - 1, 1, 1);
    display->setColor(WHITE);

    // === Draw Content ===
    int16_t lineY = boxTop + vPadding;
    uint8_t linesShown = 0;

    for (int i = 0; i < lineCount && linesShown < visibleTotalLines; i++, linesShown++) {
        strncpy(lineBuffer, lineStarts[i], 40);
        lineBuffer[lineLengths[i] > 39 ? 39 : lineLengths[i]] = '\0';

        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;
        if (needs_bell && i == 0) {
            int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + lineWidths[i] + 2, bellY, 8, 8, bell_alert);
        }

        // Determine if this is a pop-up or a pick list
        if (alertBannerOptions > 0) {
            // Pick List
            display->setColor(WHITE);
            int background_yOffset = 1;
            // Determine if we have low hanging characters
            if (strchr(lineBuffer, 'p') || strchr(lineBuffer, 'g') || strchr(lineBuffer, 'y') || strchr(lineBuffer, 'j')) {
                background_yOffset = -1;
            }
            display->fillRect(boxLeft, boxTop + 1, boxWidth, effectiveLineHeight - background_yOffset);
            display->setColor(BLACK);
            int yOffset = 3;
            display->drawString(textX, lineY - yOffset, lineBuffer);
            display->setColor(WHITE);

            lineY += (effectiveLineHeight - 2 - background_yOffset);
        } else {
            // Pop-up
            display->drawString(textX, lineY, lineBuffer);
            lineY += (effectiveLineHeight);
        }
    }

    uint8_t firstOptionToShow = 0;
    if (alertBannerOptions > 0) {
        if (curSelected > 1 && alertBannerOptions > visibleTotalLines - lineCount)
            firstOptionToShow = curSelected - 1;
        else
            firstOptionToShow = 0;
    }

    for (int i = firstOptionToShow; i < alertBannerOptions && linesShown < visibleTotalLines; i++, linesShown++) {
        if (i == curSelected) {
            strncpy(lineBuffer, "> ", 3);
            strncpy(lineBuffer + 2, optionsArrayPtr[i], 36);
            strncpy(lineBuffer + strlen(optionsArrayPtr[i]) + 2, " <", 3);
            lineBuffer[39] = '\0';
        } else {
            strncpy(lineBuffer, optionsArrayPtr[i], 40);
            lineBuffer[39] = '\0';
        }

        int16_t textX = boxLeft + (boxWidth - optionWidths[i] - (i == curSelected ? arrowsWidth : 0)) / 2;
        display->drawString(textX, lineY, lineBuffer);
        lineY += effectiveLineHeight;
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

bool NotificationRenderer::isOverlayBannerShowing()
{
    return strlen(alertBannerMessage) > 0 && (alertBannerUntil == 0 || millis() <= alertBannerUntil);
}

} // namespace graphics
#endif