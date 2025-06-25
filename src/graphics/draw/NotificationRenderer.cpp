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
    // Exit if no message is active or duration has passed
    if (!isOverlayBannerShowing())
        return;

    if (pauseBanner)
        return;

    // === Layout Configuration ===
    constexpr uint16_t padding = 5;    // Padding around text inside the box
    constexpr uint16_t vPadding = 2;   // Padding around text inside the box
    constexpr uint8_t lineSpacing = 1; // Extra space between lines
    const uint8_t lineHeight = FONT_HEIGHT_SMALL - 5;
    const uint16_t screenMargin = 4; // how far the box must stay from top/bottom of screen

    // Search the message to determine if we need the bell added
    bool needs_bell = (strstr(alertBannerMessage, "Alert Received") != nullptr);

    uint8_t firstOptionToShow = 0;

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT); // We will manually center per line
    const int MAX_LINES = 5;
    uint16_t optionWidths[alertBannerOptions] = {0};
    uint16_t maxWidth = 0;
    uint16_t arrowsWidth = display->getStringWidth(">  <", 4, true);
    uint16_t lineWidths[MAX_LINES] = {0};
    uint16_t lineLengths[MAX_LINES] = {0};
    char *lineStarts[MAX_LINES + 1];
    uint16_t lineCount = 0;
    char lineBuffer[40] = {0};
    // pointer to the terminating null
    char *alertEnd = alertBannerMessage + strnlen(alertBannerMessage, sizeof(alertBannerMessage));
    lineStarts[lineCount] = alertBannerMessage;

    // loop through lines finding \n characters
    while ((lineCount < MAX_LINES) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find(lineStarts[lineCount], alertEnd, '\n');
        lineLengths[lineCount] = lineStarts[lineCount + 1] - lineStarts[lineCount];
        if (lineStarts[lineCount + 1][0] == '\n') {
            lineStarts[lineCount + 1] += 1; // Move the start pointer beyond the \n
        }
        lineWidths[lineCount] = display->getStringWidth(lineStarts[lineCount], lineLengths[lineCount], true);
        if (lineWidths[lineCount] > maxWidth) {
            maxWidth = lineWidths[lineCount];
        }
        lineCount++;
    }

    if (alertBannerOptions > 0) {
        for (int i = 0; i < alertBannerOptions; i++) {
            optionWidths[i] = display->getStringWidth(optionsArrayPtr[i], strlen(optionsArrayPtr[i]), true);
            if (optionWidths[i] > maxWidth) {
                maxWidth = optionWidths[i];
            }
            if (optionWidths[i] + arrowsWidth > maxWidth) {
                maxWidth = optionWidths[i] + arrowsWidth;
            }
        }

        // respond to input
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
            alertBannerMessage[0] = '\0'; // end the alert early
        }
    }
    inEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    uint16_t boxWidth = padding * 2 + maxWidth;
    if (needs_bell) {
        if (isHighResolution && boxWidth <= 150) {
            boxWidth += 26;
        }
        if (!isHighResolution && boxWidth <= 100) {
            boxWidth += 20;
        }
    }

    // === Determine total content height ===
    uint16_t contentHeight = (lineCount + alertBannerOptions) * lineHeight + vPadding * 2;
    uint16_t maxBoxHeight = display->height() - screenMargin * 2;

    uint16_t boxHeight = contentHeight;
    if (boxHeight > maxBoxHeight) {
        boxHeight = maxBoxHeight;
    }

    // center the box vertically
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);
    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);

    // Calculate how many option rows can fit below the lines
    uint8_t availableRows = (boxHeight - vPadding * 2) / lineHeight;
    uint8_t maxOptionsVisible = (availableRows > lineCount) ? (availableRows - lineCount) : 0;

    if (curSelected >= maxOptionsVisible && alertBannerOptions > maxOptionsVisible) {
        firstOptionToShow = curSelected - (maxOptionsVisible - 1); // Show selected near bottom
    } else {
        firstOptionToShow = 0;
    }

    // === Draw background box ===
    display->setColor(BLACK);
    display->fillRect(boxLeft - 1, boxTop - 1, boxWidth + 2, boxHeight + 2); // Slightly oversized box
    display->fillRect(boxLeft, boxTop - 2, boxWidth, 1);                     // Top Line
    display->fillRect(boxLeft, boxTop + boxHeight + 1, boxWidth, 1);         // Bottom Line
    display->fillRect(boxLeft - 2, boxTop, 1, boxHeight);                    // Left Line
    display->fillRect(boxLeft + boxWidth + 1, boxTop, 1, boxHeight);         // Right Line
    display->setColor(WHITE);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight); // Border
    display->setColor(BLACK);
    display->fillRect(boxLeft, boxTop, 1, 1);                                // Top Left
    display->fillRect(boxLeft + boxWidth - 1, boxTop, 1, 1);                 // Top Right
    display->fillRect(boxLeft, boxTop + boxHeight - 1, 1, 1);                // Bottom Left
    display->fillRect(boxLeft + boxWidth - 1, boxTop + boxHeight - 1, 1, 1); // Bottom Right
    display->setColor(WHITE);

    // === Draw each line centered in the box ===
    int16_t lineY = boxTop + vPadding - 1;

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
        lineY += lineHeight;

        if (i == lineCount - 1) {
            const uint8_t extraOffset = 4; // you can tweak this value (e.g. 5 or 6 for more space)
            int16_t separatorY = lineY + extraOffset - 1;
            display->drawLine(boxLeft + padding, separatorY, boxLeft + boxWidth - padding, separatorY);
            lineY = separatorY + 1; // ensure options appear below the line
        }
    }

    for (int i = 0; i < alertBannerOptions; i++) {
        if (i >= firstOptionToShow && i < firstOptionToShow + maxOptionsVisible) {
            if (i == curSelected) {
                strncpy(lineBuffer, "> ", 3);
                strncpy(lineBuffer + 2, optionsArrayPtr[i], 36);
                strncpy(lineBuffer + strlen(optionsArrayPtr[i]) + 2, " <", 3);
                optionWidths[i] += arrowsWidth;
                lineBuffer[39] = '\0';
            } else {
                strncpy(lineBuffer, optionsArrayPtr[i], 40);
                lineBuffer[39] = '\0';
            }

            int16_t textX = boxLeft + (boxWidth - optionWidths[i]) / 2;

            if (needs_bell && i == 0) {
                int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
                display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
                display->drawXbm(textX + optionWidths[i] + 2, bellY, 8, 8, bell_alert);
            }

            display->drawString(textX, lineY, lineBuffer);
            lineY += lineHeight;
        }
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