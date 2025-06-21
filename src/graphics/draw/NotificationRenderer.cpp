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

    // Search the message to determine if we need the bell added
    bool needs_bell = (strstr(alertBannerMessage, "Alert Received") != nullptr);

    uint8_t firstOption = 0;
    uint8_t firstOptionToShow = 0;

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT); // We will manually center per line
    const int MAX_LINES = 24;

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
        if (alertBannerOptions > 0 && lineCount > 0 && lineWidths[lineCount] + arrowsWidth > maxWidth) {
            maxWidth = lineWidths[lineCount] + arrowsWidth;
        }
        lineCount++;
        // if we are doing a selection, add extra width for arrows
    }

    if (alertBannerOptions > 0) {
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
        // compare number of options to number of lines
        if (lineCount < alertBannerOptions)
            return;
        firstOption = lineCount - alertBannerOptions;
        if (curSelected > 1 && alertBannerOptions > 3) {
            firstOptionToShow = curSelected + firstOption - 1;
            // put the selected option in the middle
        } else {
            firstOptionToShow = firstOption;
        }
    } else { // not in an alert with a callback
        // TODO: check that at least a second has passed since the alert started
        if (inEvent == INPUT_BROKER_SELECT || inEvent == INPUT_BROKER_ALT_LONG || inEvent == INPUT_BROKER_CANCEL) {
            alertBannerMessage[0] = '\0'; // end the alert early
        }
    }
    inEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    // set width from longest line
    uint16_t boxWidth = padding * 2 + maxWidth;
    if (needs_bell) {
        if (SCREEN_WIDTH > 128 && boxWidth <= 150) {
            boxWidth += 26;
        }
        if (SCREEN_WIDTH <= 128 && boxWidth <= 100) {
            boxWidth += 20;
        }
    }
    // calculate max lines on screen? for now it's 4
    // set height from line count
    uint16_t boxHeight;
    if (lineCount <= 4) {
        boxHeight = vPadding * 2 + lineCount * FONT_HEIGHT_SMALL + (lineCount - 1) * lineSpacing;
    } else {
        boxHeight = vPadding * 2 + 4 * FONT_HEIGHT_SMALL + 4 * lineSpacing;
    }

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);
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
    int16_t lineY = boxTop + vPadding;

    for (int i = 0; i < lineCount; i++) {
        // is this line selected?
        // if so, start the buffer with -> and strncpy to the 4th location
        if (i < lineCount - alertBannerOptions || alertBannerOptions == 0) {
            strncpy(lineBuffer, lineStarts[i], 40);
            if (lineLengths[i] > 39)
                lineBuffer[39] = '\0';
            else
                lineBuffer[lineLengths[i]] = '\0';
        } else if (i >= firstOptionToShow && i < firstOptionToShow + 3) {
            if (i == curSelected + firstOption) {
                if (lineLengths[i] > 35)
                    lineLengths[i] = 35;
                strncpy(lineBuffer, "> ", 3);
                strncpy(lineBuffer + 2, lineStarts[i], 36);
                strncpy(lineBuffer + lineLengths[i] + 2, " <", 3);
                lineLengths[i] += 4;
                lineWidths[i] += display->getStringWidth(">  <", 4, true);
                if (lineLengths[i] > 35)
                    lineBuffer[39] = '\0';
                else
                    lineBuffer[lineLengths[i]] = '\0';
            } else {
                strncpy(lineBuffer, lineStarts[i], 40);
                if (lineLengths[i] > 39)
                    lineBuffer[39] = '\0';
                else
                    lineBuffer[lineLengths[i]] = '\0';
            }
        } else { // add break for the additional lines
            continue;
        }

        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;

        if (needs_bell && i == 0) {
            int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + lineWidths[i] + 2, bellY, 8, 8, bell_alert);
        }

        display->drawString(textX, lineY, lineBuffer);
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

bool NotificationRenderer::isOverlayBannerShowing()
{
    return strlen(alertBannerMessage) > 0 && (alertBannerUntil == 0 || millis() <= alertBannerUntil);
}

} // namespace graphics
#endif