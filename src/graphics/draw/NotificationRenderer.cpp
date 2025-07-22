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

InputEvent NotificationRenderer::inEvent;
int8_t NotificationRenderer::curSelected = 0;
char NotificationRenderer::alertBannerMessage[256] = {0};
uint32_t NotificationRenderer::alertBannerUntil = 0;  // 0 is a special case meaning forever
uint8_t NotificationRenderer::alertBannerOptions = 0; // last x lines are seelctable options
const char **NotificationRenderer::optionsArrayPtr = nullptr;
const int *NotificationRenderer::optionsEnumPtr = nullptr;
std::function<void(int)> NotificationRenderer::alertBannerCallback = NULL;
bool NotificationRenderer::pauseBanner = false;
notificationTypeEnum NotificationRenderer::current_notification_type = notificationTypeEnum::none;
uint32_t NotificationRenderer::numDigits = 0;
uint32_t NotificationRenderer::currentNumber = 0;

uint32_t pow_of_10(uint32_t n)
{
    uint32_t ret = 1;
    for (uint32_t i = 0; i < n; i++) {
        ret *= 10;
    }
    return ret;
}

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

void NotificationRenderer::resetBanner()
{
    alertBannerMessage[0] = '\0';
    current_notification_type = notificationTypeEnum::none;

    inEvent.inputEvent = INPUT_BROKER_NONE;
    inEvent.kbchar = 0;
    curSelected = 0;
    alertBannerOptions = 0; // last x lines are seelctable options
    optionsArrayPtr = nullptr;
    optionsEnumPtr = nullptr;
    alertBannerCallback = NULL;
    pauseBanner = false;
    numDigits = 0;
    currentNumber = 0;

    nodeDB->pause_sort(false);
}

void NotificationRenderer::drawBannercallback(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    if (!isOverlayBannerShowing() && alertBannerMessage[0] != '\0')
        resetBanner();
    if (!isOverlayBannerShowing() || pauseBanner)
        return;
    switch (current_notification_type) {
    case notificationTypeEnum::none:
        // Do nothing - no notification to display
        break;
    case notificationTypeEnum::text_banner:
    case notificationTypeEnum::selection_picker:
        drawAlertBannerOverlay(display, state);
        break;
    case notificationTypeEnum::node_picker:
        drawNodePicker(display, state);
        break;
    case notificationTypeEnum::number_picker:
        drawNumberPicker(display, state);
        break;
    }
}

void NotificationRenderer::drawNumberPicker(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    const char *lineStarts[MAX_LINES + 1] = {0};
    uint16_t lineCount = 0;

    // Parse lines
    char *alertEnd = alertBannerMessage + strnlen(alertBannerMessage, sizeof(alertBannerMessage));
    lineStarts[lineCount] = alertBannerMessage;

    // Find lines
    while ((lineCount < MAX_LINES) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find((char *)lineStarts[lineCount], alertEnd, '\n');
        if (lineStarts[lineCount + 1][0] == '\n')
            lineStarts[lineCount + 1] += 1;
        lineCount++;
    }
    // modulo to extract
    uint8_t this_digit = (currentNumber % (pow_of_10(numDigits - curSelected))) / (pow_of_10(numDigits - curSelected - 1));
    // Handle input
    if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_ALT_PRESS) {
        if (this_digit == 9) {
            currentNumber -= 9 * (pow_of_10(numDigits - curSelected - 1));
        } else {
            currentNumber += (pow_of_10(numDigits - curSelected - 1));
        }
    } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_USER_PRESS) {
        if (this_digit == 0) {
            currentNumber += 9 * (pow_of_10(numDigits - curSelected - 1));
        } else {
            currentNumber -= (pow_of_10(numDigits - curSelected - 1));
        }
    } else if (inEvent.inputEvent == INPUT_BROKER_ANYKEY) {
        if (inEvent.kbchar > 47 && inEvent.kbchar < 58) { // have a digit
            currentNumber -= this_digit * (pow_of_10(numDigits - curSelected - 1));
            currentNumber += (inEvent.kbchar - 48) * (pow_of_10(numDigits - curSelected - 1));
            curSelected++;
        }
    } else if (inEvent.inputEvent == INPUT_BROKER_SELECT || inEvent.inputEvent == INPUT_BROKER_RIGHT) {
        curSelected++;
    } else if (inEvent.inputEvent == INPUT_BROKER_LEFT) {
        curSelected--;
    } else if ((inEvent.inputEvent == INPUT_BROKER_CANCEL || inEvent.inputEvent == INPUT_BROKER_ALT_LONG) &&
               alertBannerUntil != 0) {
        resetBanner();
        return;
    }
    if (curSelected == static_cast<int8_t>(numDigits)) {
        alertBannerCallback(currentNumber);
        resetBanner();
        return;
    }

    inEvent.inputEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    uint16_t totalLines = lineCount + 2;
    const char *linePointers[totalLines + 1] = {0}; // this is sort of a dynamic allocation

    // copy the linestarts to display to the linePointers holder
    for (uint16_t i = 0; i < lineCount; i++) {
        linePointers[i] = lineStarts[i];
    }
    std::string digits = " ";
    std::string arrowPointer = " ";
    for (uint16_t i = 0; i < numDigits; i++) {
        // Modulo minus modulo to return just the current number
        digits += std::to_string((currentNumber % (pow_of_10(numDigits - i))) / (pow_of_10(numDigits - i - 1))) + " ";
        if (curSelected == i) {
            arrowPointer += "^ ";
        } else {
            arrowPointer += "_ ";
        }
    }

    linePointers[lineCount++] = digits.c_str();
    linePointers[lineCount++] = arrowPointer.c_str();

    drawNotificationBox(display, state, linePointers, totalLines, 0);
}

void NotificationRenderer::drawNodePicker(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    static uint32_t selectedNodenum = 0;

    // === Layout Configuration ===
    constexpr uint16_t vPadding = 2;
    alertBannerOptions = nodeDB->getNumMeshNodes() - 1;

    // let the box drawing function calculate the widths?

    const char *lineStarts[MAX_LINES + 1] = {0};
    uint16_t lineCount = 0;

    // Parse lines
    char *alertEnd = alertBannerMessage + strnlen(alertBannerMessage, sizeof(alertBannerMessage));
    lineStarts[lineCount] = alertBannerMessage;

    while ((lineCount < MAX_LINES) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find((char *)lineStarts[lineCount], alertEnd, '\n');
        if (lineStarts[lineCount + 1][0] == '\n')
            lineStarts[lineCount + 1] += 1;
        lineCount++;
    }

    // Handle input
    if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_ALT_PRESS) {
        curSelected--;
    } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_USER_PRESS) {
        curSelected++;
    } else if (inEvent.inputEvent == INPUT_BROKER_SELECT) {
        alertBannerCallback(selectedNodenum);
        resetBanner();
        return;
    } else if ((inEvent.inputEvent == INPUT_BROKER_CANCEL || inEvent.inputEvent == INPUT_BROKER_ALT_LONG) &&
               alertBannerUntil != 0) {
        resetBanner();
        return;
    }

    if (curSelected == -1)
        curSelected = alertBannerOptions - 1;
    if (curSelected == alertBannerOptions)
        curSelected = 0;

    inEvent.inputEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    uint16_t totalLines = lineCount + alertBannerOptions;
    uint16_t screenHeight = display->height();
    uint8_t effectiveLineHeight = FONT_HEIGHT_SMALL - 3;
    uint8_t visibleTotalLines = std::min<uint8_t>(totalLines, (screenHeight - vPadding * 2) / effectiveLineHeight);
    uint8_t linesShown = lineCount;
    const char *linePointers[visibleTotalLines + 1] = {0}; // this is sort of a dynamic allocation

    // copy the linestarts to display to the linePointers holder
    for (int i = 0; i < lineCount; i++) {
        linePointers[i] = lineStarts[i];
    }
    char scratchLineBuffer[visibleTotalLines - lineCount][40];

    uint8_t firstOptionToShow = 0;
    if (curSelected > 1 && alertBannerOptions > visibleTotalLines - lineCount) {
        if (curSelected > alertBannerOptions - visibleTotalLines + lineCount)
            firstOptionToShow = alertBannerOptions - visibleTotalLines + lineCount;
        else
            firstOptionToShow = curSelected - 1;
    } else {
        firstOptionToShow = 0;
    }
    int scratchLineNum = 0;
    for (int i = firstOptionToShow; i < alertBannerOptions && linesShown < visibleTotalLines; i++, linesShown++) {
        char temp_name[16] = {0};
        if (nodeDB->getMeshNodeByIndex(i + 1)->has_user) {
            std::string sanitized = sanitizeString(nodeDB->getMeshNodeByIndex(i + 1)->user.long_name);
            strncpy(temp_name, sanitized.c_str(), sizeof(temp_name) - 1);

        } else {
            snprintf(temp_name, sizeof(temp_name), "(%04X)", (uint16_t)(nodeDB->getMeshNodeByIndex(i + 1)->num & 0xFFFF));
        }
        // make temp buffer for name
        // fi
        if (i == curSelected) {
            selectedNodenum = nodeDB->getMeshNodeByIndex(i + 1)->num;
            if (isHighResolution) {
                strncpy(scratchLineBuffer[scratchLineNum], "> ", 3);
                strncpy(scratchLineBuffer[scratchLineNum] + 2, temp_name, 36);
                strncpy(scratchLineBuffer[scratchLineNum] + strlen(temp_name) + 2, " <", 3);
            } else {
                strncpy(scratchLineBuffer[scratchLineNum], ">", 2);
                strncpy(scratchLineBuffer[scratchLineNum] + 1, temp_name, 37);
                strncpy(scratchLineBuffer[scratchLineNum] + strlen(temp_name) + 1, "<", 2);
            }
            scratchLineBuffer[scratchLineNum][39] = '\0';
        } else {
            strncpy(scratchLineBuffer[scratchLineNum], temp_name, 36);
        }
        linePointers[linesShown] = scratchLineBuffer[scratchLineNum++];
    }
    drawNotificationBox(display, state, linePointers, totalLines, firstOptionToShow);
}

void NotificationRenderer::drawAlertBannerOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // === Layout Configuration ===
    constexpr uint16_t vPadding = 2;

    uint16_t optionWidths[alertBannerOptions] = {0};
    uint16_t maxWidth = 0;
    uint16_t arrowsWidth = display->getStringWidth(">  <", 4, true);
    uint16_t lineWidths[MAX_LINES] = {0};
    uint16_t lineLengths[MAX_LINES] = {0};
    const char *lineStarts[MAX_LINES + 1] = {0};
    uint16_t lineCount = 0;
    char lineBuffer[40] = {0};

    // Parse lines
    char *alertEnd = alertBannerMessage + strnlen(alertBannerMessage, sizeof(alertBannerMessage));
    lineStarts[lineCount] = alertBannerMessage;

    while ((lineCount < MAX_LINES) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find((char *)lineStarts[lineCount], alertEnd, '\n');
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
        if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_ALT_PRESS) {
            curSelected--;
        } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_USER_PRESS) {
            curSelected++;
        } else if (inEvent.inputEvent == INPUT_BROKER_SELECT) {
            if (optionsEnumPtr != nullptr) {
                alertBannerCallback(optionsEnumPtr[curSelected]);
                optionsEnumPtr = nullptr;
            } else {
                alertBannerCallback(curSelected);
            }
            resetBanner();
            return;
        } else if ((inEvent.inputEvent == INPUT_BROKER_CANCEL || inEvent.inputEvent == INPUT_BROKER_ALT_LONG) &&
                   alertBannerUntil != 0) {
            resetBanner();
            return;
        }

        if (curSelected == -1)
            curSelected = alertBannerOptions - 1;
        if (curSelected == alertBannerOptions)
            curSelected = 0;
    } else {
        if (inEvent.inputEvent == INPUT_BROKER_SELECT || inEvent.inputEvent == INPUT_BROKER_ALT_LONG ||
            inEvent.inputEvent == INPUT_BROKER_CANCEL) {
            resetBanner();
            return;
        }
    }

    inEvent.inputEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    uint16_t totalLines = lineCount + alertBannerOptions;

    uint16_t screenHeight = display->height();
    uint8_t effectiveLineHeight = FONT_HEIGHT_SMALL - 3;
    uint8_t visibleTotalLines = std::min<uint8_t>(totalLines, (screenHeight - vPadding * 2) / effectiveLineHeight);
    uint8_t linesShown = lineCount;
    const char *linePointers[visibleTotalLines + 1] = {0}; // this is sort of a dynamic allocation

    // copy the linestarts to display to the linePointers holder
    for (int i = 0; i < lineCount; i++) {
        linePointers[i] = lineStarts[i];
    }

    uint8_t firstOptionToShow = 0;
    if (alertBannerOptions > 0) {
        if (curSelected > 1 && alertBannerOptions > visibleTotalLines - lineCount) {
            if (curSelected > alertBannerOptions - visibleTotalLines + lineCount)
                firstOptionToShow = alertBannerOptions - visibleTotalLines + lineCount;
            else
                firstOptionToShow = curSelected - 1;
        } else {
            firstOptionToShow = 0;
        }
    }

    for (int i = firstOptionToShow; i < alertBannerOptions && linesShown < visibleTotalLines; i++, linesShown++) {
        if (i == curSelected) {
            if (isHighResolution) {
                strncpy(lineBuffer, "> ", 3);
                strncpy(lineBuffer + 2, optionsArrayPtr[i], 36);
                strncpy(lineBuffer + strlen(optionsArrayPtr[i]) + 2, " <", 3);
            } else {
                strncpy(lineBuffer, ">", 2);
                strncpy(lineBuffer + 1, optionsArrayPtr[i], 37);
                strncpy(lineBuffer + strlen(optionsArrayPtr[i]) + 1, "<", 2);
            }
            lineBuffer[39] = '\0';
            linePointers[linesShown] = lineBuffer;
        } else {
            linePointers[linesShown] = optionsArrayPtr[i];
        }
    }
    if (alertBannerOptions > 0) {
        drawNotificationBox(display, state, linePointers, totalLines, firstOptionToShow, maxWidth);
    } else {
        drawNotificationBox(display, state, linePointers, totalLines, firstOptionToShow);
    }
}

void NotificationRenderer::drawNotificationBox(OLEDDisplay *display, OLEDDisplayUiState *state, const char *lines[],
                                               uint16_t totalLines, uint8_t firstOptionToShow, uint16_t maxWidth)
{

    bool is_picker = false;
    uint16_t lineCount = 0;
    // === Layout Configuration ===
    constexpr uint16_t hPadding = 5;
    constexpr uint16_t vPadding = 2;
    bool needs_bell = false;
    uint16_t lineWidths[totalLines] = {0};
    uint16_t lineLengths[totalLines] = {0};

    if (maxWidth != 0)
        is_picker = true;

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    while (lines[lineCount] != nullptr) {
        auto newlinePointer = strchr(lines[lineCount], '\n');
        if (newlinePointer)
            lineLengths[lineCount] = (newlinePointer - lines[lineCount]); // Check for newlines first
        else // if the newline wasn't found, then pull string length from strlen
            lineLengths[lineCount] = strlen(lines[lineCount]);
        lineWidths[lineCount] = display->getStringWidth(lines[lineCount], lineLengths[lineCount], true);
        if (!is_picker) {
            needs_bell |= (strstr(alertBannerMessage, "Alert Received") != nullptr);
            if (lineWidths[lineCount] > maxWidth)
                maxWidth = lineWidths[lineCount];
        }
        lineCount++;
    }
    // count lines

    uint16_t boxWidth = hPadding * 2 + maxWidth;
    if (needs_bell) {
        if (isHighResolution && boxWidth <= 150)
            boxWidth += 26;
        if (!isHighResolution && boxWidth <= 100)
            boxWidth += 20;
    }

    uint16_t screenHeight = display->height();
    uint8_t effectiveLineHeight = FONT_HEIGHT_SMALL - 3;
    uint8_t visibleTotalLines = std::min<uint8_t>(lineCount, (screenHeight - vPadding * 2) / effectiveLineHeight);
    uint16_t contentHeight = visibleTotalLines * effectiveLineHeight;
    uint16_t boxHeight = contentHeight + vPadding * 2;
    if (visibleTotalLines == 1) {
        boxHeight += (isHighResolution) ? 4 : 3;
    }

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    if (totalLines > visibleTotalLines) {
        boxWidth += (isHighResolution) ? 4 : 2;
    }
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
    for (int i = 0; i < lineCount; i++) {
        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;
        if (needs_bell && i == 0) {
            int bellY = lineY + (FONT_HEIGHT_SMALL - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + lineWidths[i] + 2, bellY, 8, 8, bell_alert);
        }
        char lineBuffer[lineLengths[i] + 1];
        strncpy(lineBuffer, lines[i], lineLengths[i]);
        lineBuffer[lineLengths[i]] = '\0';
        // Determine if this is a pop-up or a pick list
        if (alertBannerOptions > 0 && i == 0) {
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

    // === Scroll Bar (Thicker, inside box, not over title) ===
    if (totalLines > visibleTotalLines) {
        const uint8_t scrollBarWidth = 5;

        int16_t scrollBarX = boxLeft + boxWidth - scrollBarWidth - 2;
        int16_t scrollBarY = boxTop + vPadding + effectiveLineHeight; // start after title line
        uint16_t scrollBarHeight = boxHeight - vPadding * 2 - effectiveLineHeight;

        float ratio = (float)visibleTotalLines / totalLines;
        uint16_t indicatorHeight = std::max((int)(scrollBarHeight * ratio), 4);
        float scrollRatio = (float)(firstOptionToShow + lineCount - visibleTotalLines) / (totalLines - visibleTotalLines);
        uint16_t indicatorY = scrollBarY + scrollRatio * (scrollBarHeight - indicatorHeight);

        display->drawRect(scrollBarX, scrollBarY, scrollBarWidth, scrollBarHeight);
        display->fillRect(scrollBarX + 1, indicatorY, scrollBarWidth - 2, indicatorHeight);
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