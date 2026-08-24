#include "configuration.h"

#if HAS_SCREEN
#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "NotificationRenderer.h"
#include "UIRenderer.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
#include "graphics/TouchLayout.h"
#endif
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/images.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#if defined(_VARIANT_T_DECK_MAX)
#include "MenuHandler.h"
#include "platform/extra_variants/t_deck_max/TDeckMaxTouch.h"
#endif
#include "mesh/Throttle.h"
#if HAS_BUTTON
#include "input/ButtonThread.h"
#endif
#include "main.h"
#include <algorithm>
#include <string>
#include <vector>
#if HAS_TRACKBALL
#include "input/TrackballInterruptImpl1.h"
#endif

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#endif

using namespace meshtastic;

#if HAS_BUTTON
// Global button thread pointer defined in main.cpp
extern ::ButtonThread *UserButtonThread;
#endif

// External references to global variables from Screen.cpp
extern std::vector<std::string> functionSymbol;
extern std::string functionSymbolString;
extern bool hasUnreadMessage;

namespace graphics
{
int bannerSignalBars = -1;
InputEvent NotificationRenderer::inEvent;
int8_t NotificationRenderer::curSelected = 0;
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
bool NotificationRenderer::touchSelectionPending = false;
#endif
char NotificationRenderer::alertBannerMessage[256] = {0};
uint32_t NotificationRenderer::alertBannerUntil = 0;  // 0 is a special case meaning forever
uint8_t NotificationRenderer::alertBannerOptions = 0; // last x lines are selectable options
const char **NotificationRenderer::optionsArrayPtr = nullptr;
const int *NotificationRenderer::optionsEnumPtr = nullptr;
std::function<void(int)> NotificationRenderer::alertBannerCallback = NULL;
bool NotificationRenderer::pauseBanner = false;
notificationTypeEnum NotificationRenderer::current_notification_type = notificationTypeEnum::none;
uint32_t NotificationRenderer::numDigits = 0;
uint32_t NotificationRenderer::currentNumber = 0;
char NotificationRenderer::alphanumericValue[16] = {0};
VirtualKeyboard *NotificationRenderer::virtualKeyboard = nullptr;
std::function<void(const std::string &)> NotificationRenderer::textInputCallback = nullptr;

uint32_t pow_of_10(uint32_t n)
{
    uint32_t ret = 1;
    for (uint32_t i = 0; i < n; i++) {
        ret *= 10;
    }
    return ret;
}

uint64_t pow_of_16(uint32_t n)
{
    uint64_t ret = 1;
    for (uint32_t i = 0; i < n; i++) {
        ret *= 16ULL;
    }
    return ret;
}

char graphics::NotificationRenderer::alertBannerLines[MAX_LINES + 1][64] = {};
uint8_t graphics::NotificationRenderer::alertBannerLineCount = 0;
graphics::NotificationRenderer::BannerFont graphics::NotificationRenderer::alertBannerLineFonts[MAX_LINES + 1] = {};

static inline graphics::NotificationRenderer::BannerFont parseFontTagPrefix(const char *&p)
{
    // Tags must be at the start of the line:
    // [S] small, [M] medium, [L] large
    if (p && p[0] == '[' && p[1] != '\0' && p[2] == ']') {
        char t = p[1];
        if (t == 'S') {
            p += 3;
            return graphics::NotificationRenderer::BANNER_FONT_SMALL;
        }
        if (t == 'M') {
            p += 3;
            return graphics::NotificationRenderer::BANNER_FONT_MEDIUM;
        }
        if (t == 'L') {
            p += 3;
            return graphics::NotificationRenderer::BANNER_FONT_LARGE;
        }
    }
    return graphics::NotificationRenderer::BANNER_FONT_DEFAULT;
}

static inline const uint8_t *fontForBannerLine(graphics::NotificationRenderer::BannerFont f)
{
    switch (f) {
    case graphics::NotificationRenderer::BANNER_FONT_SMALL:
        return FONT_SMALL;
    case graphics::NotificationRenderer::BANNER_FONT_MEDIUM:
        return FONT_MEDIUM;
    case graphics::NotificationRenderer::BANNER_FONT_LARGE:
        return FONT_LARGE;
    case graphics::NotificationRenderer::BANNER_FONT_DEFAULT:
    default:
        return FONT_SMALL;
    }
}

static inline uint8_t effectiveLineHeightForBannerLine(graphics::NotificationRenderer::BannerFont f)
{
    uint8_t height = FONT_HEIGHT_SMALL;
    switch (f) {
    case graphics::NotificationRenderer::BANNER_FONT_MEDIUM:
        height = FONT_HEIGHT_MEDIUM;
        break;
    case graphics::NotificationRenderer::BANNER_FONT_LARGE:
        height = FONT_HEIGHT_LARGE;
        break;
    case graphics::NotificationRenderer::BANNER_FONT_SMALL:
    case graphics::NotificationRenderer::BANNER_FONT_DEFAULT:
    default:
        height = FONT_HEIGHT_SMALL;
        break;
    }
    return (height > 3) ? (height - 3) : height;
}

const char *graphics::NotificationRenderer::resolveBannerLine(uint16_t lineIndex, const char *rawLine, BannerFont &lineFont)
{
    lineFont = BANNER_FONT_DEFAULT;
    bool tagAware = (current_notification_type == notificationTypeEnum::text_banner ||
                     current_notification_type == notificationTypeEnum::pairing_pin) &&
                    alertBannerOptions == 0;
    if (!tagAware)
        return rawLine;
    if (lineIndex < alertBannerLineCount) {
        lineFont = alertBannerLineFonts[lineIndex];
        return alertBannerLines[lineIndex];
    }
    // The parsed-line cache doesn't cover this line (the banner text was stored without a
    // re-parse, or a draw raced the parse from another task): strip the tag here too, so it
    // acts as a font change and never renders as literal text - the BLE pair PIN banner
    // prefixes its PIN line with [M].
    lineFont = parseFontTagPrefix(rawLine);
    return rawLine;
}

void graphics::NotificationRenderer::parseBannerMessageWithFonts(const char *message)
{
    alertBannerLineCount = 0;
    for (uint8_t i = 0; i < (MAX_LINES + 1); i++) {
        alertBannerLines[i][0] = '\0';
        alertBannerLineFonts[i] = BANNER_FONT_DEFAULT;
    }

    if (!message || !message[0]) {
        return;
    }

    const char *p = message;

    while (*p && alertBannerLineCount < (MAX_LINES + 1)) {
        const char *lineStart = p;
        while (*p && *p != '\n') {
            p++;
        }

        char tmp[64] = {0};
        size_t len = (size_t)(p - lineStart);
        if (len > (sizeof(tmp) - 1)) {
            len = sizeof(tmp) - 1;
        }
        memcpy(tmp, lineStart, len);
        tmp[len] = '\0';

        // Tag at start
        const char *tp = tmp;
        BannerFont f = parseFontTagPrefix(tp);
        alertBannerLineFonts[alertBannerLineCount] = f;

        // Store stripped text
        strncpy(alertBannerLines[alertBannerLineCount], tp, sizeof(alertBannerLines[0]) - 1);
        alertBannerLines[alertBannerLineCount][sizeof(alertBannerLines[0]) - 1] = '\0';
        alertBannerLineCount++;

        if (*p == '\n') {
            p++;
        }
    }
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
    notificationTypeEnum previousType = current_notification_type;

    alertBannerMessage[0] = '\0';
    current_notification_type = notificationTypeEnum::none;

    inEvent.inputEvent = INPUT_BROKER_NONE;
    inEvent.kbchar = 0;
    curSelected = 0;
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    touchSelectionPending = false;
#endif
    alertBannerOptions = 0; // last x lines are selectable options
    optionsArrayPtr = nullptr;
    optionsEnumPtr = nullptr;
    alertBannerCallback = NULL;
    pauseBanner = false;
    numDigits = 0;
    currentNumber = 0;

    nodeDB->pause_sort(false);

    // If we're exiting from text_input (virtual keyboard), stop module and trigger frame update
    // to ensure any messages received during keyboard use are now displayed
    if (previousType == notificationTypeEnum::text_input && screen) {
        OnScreenKeyboardModule::instance().stop(false);
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    }
}

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
bool NotificationRenderer::handleTouchTarget(uint32_t value)
{
    if (alertBannerOptions == 0 || value >= alertBannerOptions)
        return false;

    meshtastic_NodeInfoLite *node = nullptr;
    if (current_notification_type == notificationTypeEnum::node_picker) {
        node = nodeDB->getMeshNodeByIndex(value + 1);
        if (!node)
            return false;
    }

    const int8_t requestedSelection = static_cast<int8_t>(value);
    if (!touchSelectionPending || curSelected != requestedSelection) {
        curSelected = requestedSelection;
        touchSelectionPending = true;
        return true;
    }

    if (node) {
        if (alertBannerCallback)
            alertBannerCallback(node->num);
    } else if (optionsEnumPtr != nullptr) {
        if (alertBannerCallback)
            alertBannerCallback(optionsEnumPtr[value]);
    } else if (alertBannerCallback) {
        alertBannerCallback(static_cast<int>(value));
    }

#if defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    if (current_notification_type == notificationTypeEnum::text_input) {
        // The option callback may have replaced this menu with the touch keyboard.
        // Do not resetBanner() here because it would stop the newly opened keyboard.
        alertBannerOptions = 0;
        optionsArrayPtr = nullptr;
        optionsEnumPtr = nullptr;
        alertBannerCallback = nullptr;
        touchSelectionPending = false;
    } else {
        resetBanner();
    }
#else
    resetBanner();
#endif
    return true;
}
#endif

#if defined(_VARIANT_T_DECK_MAX)
bool NotificationRenderer::handleMaxTouchKeyRight(const InputEvent *event)
{
    if (event == nullptr || event->inputEvent != INPUT_BROKER_RIGHT ||
        !t_deck_max::isMaxTouchKeySource(event->source))
        return false;

    if (current_notification_type == notificationTypeEnum::text_input ||
        current_notification_type == notificationTypeEnum::number_picker ||
        current_notification_type == notificationTypeEnum::hex_picker ||
        current_notification_type == notificationTypeEnum::alphanumeric_picker) {
        return false;
    }

    if (current_notification_type == notificationTypeEnum::node_picker) {
        resetBanner();
        menuHandler::menuQueue = menuHandler::NodeBaseMenu;
        return true;
    }

    if (alertBannerOptions == 0 || optionsArrayPtr == nullptr ||
        !t_deck_max::isSafeMaxMenuBackLabel(optionsArrayPtr[0]))
        return false;

    if (alertBannerCallback) {
        alertBannerCallback(optionsEnumPtr != nullptr ? optionsEnumPtr[0] : 0);
    }
    resetBanner();
    return true;
}
#endif

void NotificationRenderer::drawBannercallback(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // Handle text_input notifications first - they have their own timeout/banner logic
    if (current_notification_type == notificationTypeEnum::text_input) {
        // Check for timeout and reset if needed for text input
        if (alertBannerUntil > 0 && Throttle::deadlinePassed(alertBannerUntil)) {
            resetBanner();
            return;
        }
        drawTextInput(display, state);
        return;
    }

    // 0 means "no deadline set", and reads as long expired - test it first.
    if (alertBannerUntil > 0 && Throttle::deadlinePassed(alertBannerUntil)) {
        resetBanner();
    }

    // Exit if no banner is showing or banner is paused
    if (!isOverlayBannerShowing() || pauseBanner) {
        return;
    }

    // Compact panels: DOWN cancels menus instead of scrolling (covers every picker below).
    if (graphics::isCompactPanel(display) && inEvent.inputEvent == INPUT_BROKER_DOWN) {
        inEvent.inputEvent = INPUT_BROKER_CANCEL;
    }

    switch (current_notification_type) {
    case notificationTypeEnum::none:
        // Do nothing - no notification to display
        break;
    case notificationTypeEnum::text_input:
        // Already handled above with dedicated logic (early return). Keep a case here to satisfy -Wswitch.
        break;
    case notificationTypeEnum::text_banner:
    case notificationTypeEnum::selection_picker:
    case notificationTypeEnum::pairing_pin:
        // pairing_pin is rendered the same as text_banner - it's just a
        // text banner. The split type exists only so the lockdown UI
        // short-circuit in Screen.cpp can recognise the BLE pair-PIN
        // banner as the one safe banner to composite over the LOCKED
        // frame.
        drawAlertBannerOverlay(display, state);
        break;
    case notificationTypeEnum::node_picker:
        drawNodePicker(display, state);
        break;
    case notificationTypeEnum::number_picker:
        drawNumberPicker(display, state);
        break;
    case notificationTypeEnum::hex_picker:
        drawHexPicker(display, state);
        break;
    case notificationTypeEnum::alphanumeric_picker:
        drawAlphanumericPicker(display, state);
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
    if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_ALT_PRESS ||
        inEvent.inputEvent == INPUT_BROKER_UP_LONG) {
        if (this_digit == 9) {
            currentNumber -= 9 * (pow_of_10(numDigits - curSelected - 1));
        } else {
            currentNumber += (pow_of_10(numDigits - curSelected - 1));
        }
    } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_USER_PRESS ||
               inEvent.inputEvent == INPUT_BROKER_DOWN_LONG) {
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

void NotificationRenderer::drawHexPicker(OLEDDisplay *display, OLEDDisplayUiState *state)
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
    uint8_t this_digit = (currentNumber % (pow_of_16(numDigits - curSelected))) / (pow_of_16(numDigits - curSelected - 1));
    // Handle input
    if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_ALT_PRESS ||
        inEvent.inputEvent == INPUT_BROKER_UP_LONG) {
        if (this_digit == 15) {
            currentNumber -= 15 * (pow_of_16(numDigits - curSelected - 1));
        } else {
            currentNumber += (pow_of_16(numDigits - curSelected - 1));
        }
    } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_USER_PRESS ||
               inEvent.inputEvent == INPUT_BROKER_DOWN_LONG) {
        if (this_digit == 0) {
            currentNumber += 15 * (pow_of_16(numDigits - curSelected - 1));
        } else {
            currentNumber -= (pow_of_16(numDigits - curSelected - 1));
        }
    } else if (inEvent.inputEvent == INPUT_BROKER_ANYKEY) {
        if (inEvent.kbchar > 47 && inEvent.kbchar < 58) { // have a digit
            currentNumber -= this_digit * (pow_of_16(numDigits - curSelected - 1));
            currentNumber += (inEvent.kbchar - 48) * (pow_of_16(numDigits - curSelected - 1));
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
        uint8_t digitValue = (currentNumber % (pow_of_16(numDigits - i))) / (pow_of_16(numDigits - i - 1));
        if (digitValue < 10) {
            digits += std::to_string(digitValue) + " ";
        } else if (digitValue == 10) {
            digits += "A ";
        } else if (digitValue == 11) {
            digits += "B ";
        } else if (digitValue == 12) {
            digits += "C ";
        } else if (digitValue == 13) {
            digits += "D ";
        } else if (digitValue == 14) {
            digits += "E ";
        } else if (digitValue == 15) {
            digits += "F ";
        }

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

// Arcade-style initials entry. Mirrors drawHexPicker's cursor/confirm flow, but each position
// holds a character from ALPHANUMERIC_CHARS (cycled with UP/DOWN) instead of a packed digit, and
// the assembled string is returned through textInputCallback.
void NotificationRenderer::drawAlphanumericPicker(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    static const char ALPHANUMERIC_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    constexpr int ALPHANUMERIC_COUNT = sizeof(ALPHANUMERIC_CHARS) - 1; // exclude the NUL

    const char *lineStarts[MAX_LINES + 1] = {0};
    uint16_t lineCount = 0;

    // Parse lines (identical to the number/hex pickers)
    char *alertEnd = alertBannerMessage + strnlen(alertBannerMessage, sizeof(alertBannerMessage));
    lineStarts[lineCount] = alertBannerMessage;
    while ((lineCount < MAX_LINES) && (lineStarts[lineCount] < alertEnd)) {
        lineStarts[lineCount + 1] = std::find((char *)lineStarts[lineCount], alertEnd, '\n');
        if (lineStarts[lineCount + 1][0] == '\n')
            lineStarts[lineCount + 1] += 1;
        lineCount++;
    }

    auto alphaIndex = [&](char c) -> int {
        for (int i = 0; i < ALPHANUMERIC_COUNT; i++)
            if (ALPHANUMERIC_CHARS[i] == c)
                return i;
        return 0;
    };

    // Handle input
    if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_ALT_PRESS ||
        inEvent.inputEvent == INPUT_BROKER_UP_LONG) {
        int idx = (alphaIndex(alphanumericValue[curSelected]) + 1) % ALPHANUMERIC_COUNT;
        alphanumericValue[curSelected] = ALPHANUMERIC_CHARS[idx];
    } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_USER_PRESS ||
               inEvent.inputEvent == INPUT_BROKER_DOWN_LONG) {
        int idx = (alphaIndex(alphanumericValue[curSelected]) + ALPHANUMERIC_COUNT - 1) % ALPHANUMERIC_COUNT;
        alphanumericValue[curSelected] = ALPHANUMERIC_CHARS[idx];
    } else if (inEvent.inputEvent == INPUT_BROKER_ANYKEY) {
        char k = inEvent.kbchar;
        if (k >= 'a' && k <= 'z')
            k = static_cast<char>(k - 'a' + 'A');
        if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) { // direct keyboard entry
            alphanumericValue[curSelected] = k;
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

    if (curSelected < 0)
        curSelected = 0;
    if (curSelected == static_cast<int8_t>(numDigits)) {
        auto callback = textInputCallback; // capture before clearing to avoid re-entrancy surprises
        std::string result(alphanumericValue, numDigits);
        textInputCallback = nullptr;
        resetBanner();
        if (callback)
            callback(result);
        return;
    }

    inEvent.inputEvent = INPUT_BROKER_NONE;
    if (alertBannerMessage[0] == '\0')
        return;

    uint16_t totalLines = lineCount + 2;
    const char *linePointers[totalLines + 1] = {0}; // this is sort of a dynamic allocation

    for (uint16_t i = 0; i < lineCount; i++) {
        linePointers[i] = lineStarts[i];
    }
    std::string chars = " ";
    std::string arrowPointer = " ";
    for (uint16_t i = 0; i < numDigits; i++) {
        chars += std::string(1, alphanumericValue[i]) + " ";
        arrowPointer += (curSelected == static_cast<int8_t>(i)) ? "^ " : "_ ";
    }

    linePointers[lineCount++] = chars.c_str();
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
    if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_LEFT ||
        inEvent.inputEvent == INPUT_BROKER_ALT_PRESS || inEvent.inputEvent == INPUT_BROKER_UP_LONG) {
        curSelected--;
    } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_RIGHT ||
               inEvent.inputEvent == INPUT_BROKER_USER_PRESS || inEvent.inputEvent == INPUT_BROKER_DOWN_LONG) {
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
#if (defined(_VARIANT_T_DECK_PRO_V1_1) || defined(T_DECK_MAX) || T5S3_EPD_UI_PROFILE) && defined(USE_EINK)
    constexpr uint8_t menuTitleHeight = T5S3_EPD_UI_MENU_TITLE_HEIGHT;
    constexpr uint8_t menuRowHeight = T5S3_EPD_UI_MENU_ROW_HEIGHT;
    constexpr uint8_t menuBottomPadding = T5S3_EPD_UI_MENU_BOTTOM_PADDING;
    constexpr uint8_t menuScreenMargin = T5S3_EPD_UI_MENU_SCREEN_MARGIN;
    const uint8_t maxOptionRows =
        std::max<uint8_t>(1, (screenHeight > menuTitleHeight + menuBottomPadding + menuScreenMargin * 2
                                   ? (screenHeight - menuTitleHeight - menuBottomPadding - menuScreenMargin * 2) / menuRowHeight
                                   : 1));
    uint8_t visibleTotalLines = std::min<uint8_t>(totalLines, static_cast<uint8_t>(1 + maxOptionRows));
#else
    uint8_t effectiveLineHeight = FONT_HEIGHT_SMALL - 3;
    uint8_t visibleTotalLines = std::min<uint8_t>(totalLines, (screenHeight - vPadding * 2) / effectiveLineHeight);
#endif
    uint8_t linesShown = lineCount;
    const char *linePointers[visibleTotalLines + 1] = {0}; // this is sort of a dynamic allocation

    // copy the linestarts to display to the linePointers holder
    for (int i = 0; i < lineCount; i++) {
        linePointers[i] = lineStarts[i];
    }
    char scratchLineBuffer[visibleTotalLines - lineCount][64];

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
        char tempName[48] = {0};
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i + 1);
        if (nodeInfoLiteHasUser(node)) {
            const char *rawName = nullptr;
            if (node->long_name[0]) {
                rawName = node->long_name;
            } else if (node->short_name[0]) {
                rawName = node->short_name;
            }
            if (rawName) {
                const int arrowWidth = (currentResolution == ScreenResolution::High)
                                           ? UIRenderer::measureStringWithEmotes(display, ">  <")
                                           : UIRenderer::measureStringWithEmotes(display, "><");
                const bool compactPanel = graphics::isCompactPanel(display);
                // Compact panels: box spans the full width, so just a small edge margin.
                const int margin = compactPanel ? 4 : 28;
                const int maxTextWidth = std::max(0, display->getWidth() - margin - arrowWidth);
                UIRenderer::truncateStringWithEmotes(display, rawName, tempName, sizeof(tempName), maxTextWidth,
                                                     compactPanel ? "" : "...");
            }
        } else {
            snprintf(tempName, sizeof(tempName), "(%04X)", (uint16_t)(node ? (node->num & 0xFFFF) : 0));
        }
        if (!tempName[0]) {
            snprintf(tempName, sizeof(tempName), "(%04X)", (uint16_t)(node ? (node->num & 0xFFFF) : 0));
        }
        if (i == curSelected) {
            selectedNodenum = node ? node->num : 0;
            if (currentResolution == ScreenResolution::High) {
                strncpy(scratchLineBuffer[scratchLineNum], "> ", 3);
                strncpy(scratchLineBuffer[scratchLineNum] + 2, tempName, sizeof(scratchLineBuffer[scratchLineNum]) - 3);
                scratchLineBuffer[scratchLineNum][sizeof(scratchLineBuffer[scratchLineNum]) - 1] = '\0';
                const size_t used = strnlen(scratchLineBuffer[scratchLineNum], sizeof(scratchLineBuffer[scratchLineNum]) - 1);
                strncpy(scratchLineBuffer[scratchLineNum] + used, " <", sizeof(scratchLineBuffer[scratchLineNum]) - used - 1);
            } else {
                strncpy(scratchLineBuffer[scratchLineNum], ">", 2);
                strncpy(scratchLineBuffer[scratchLineNum] + 1, tempName, sizeof(scratchLineBuffer[scratchLineNum]) - 2);
                scratchLineBuffer[scratchLineNum][sizeof(scratchLineBuffer[scratchLineNum]) - 1] = '\0';
                const size_t used = strnlen(scratchLineBuffer[scratchLineNum], sizeof(scratchLineBuffer[scratchLineNum]) - 1);
                strncpy(scratchLineBuffer[scratchLineNum] + used, "<", sizeof(scratchLineBuffer[scratchLineNum]) - used - 1);
            }
            scratchLineBuffer[scratchLineNum][sizeof(scratchLineBuffer[scratchLineNum]) - 1] = '\0';
        } else {
            strncpy(scratchLineBuffer[scratchLineNum], tempName, sizeof(scratchLineBuffer[scratchLineNum]) - 1);
            scratchLineBuffer[scratchLineNum][sizeof(scratchLineBuffer[scratchLineNum]) - 1] = '\0';
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
    bool useTaggedTextBanner = ((current_notification_type == notificationTypeEnum::text_banner ||
                                 current_notification_type == notificationTypeEnum::pairing_pin) &&
                                alertBannerOptions == 0 && alertBannerLineCount > 0);

    if (useTaggedTextBanner) {
        lineCount = std::min<uint8_t>(alertBannerLineCount, MAX_LINES);
        for (uint16_t i = 0; i < lineCount; i++) {
            lineStarts[i] = alertBannerLines[i];
            lineLengths[i] = strlen(lineStarts[i]);
            display->setFont(fontForBannerLine(alertBannerLineFonts[i]));
            lineWidths[i] = display->getStringWidth(lineStarts[i], lineLengths[i], true);
            if (lineWidths[i] > maxWidth)
                maxWidth = lineWidths[i];
        }
    } else {
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
    }

    // Measure option widths
    display->setFont(FONT_SMALL);
    for (int i = 0; i < alertBannerOptions; i++) {
        optionWidths[i] = display->getStringWidth(optionsArrayPtr[i], strlen(optionsArrayPtr[i]), true);
        if (optionWidths[i] > maxWidth)
            maxWidth = optionWidths[i];
        if (optionWidths[i] + arrowsWidth > maxWidth)
            maxWidth = optionWidths[i] + arrowsWidth;
    }

    // Handle input
    if (alertBannerOptions > 0) {
        if (inEvent.inputEvent == INPUT_BROKER_UP || inEvent.inputEvent == INPUT_BROKER_LEFT ||
            inEvent.inputEvent == INPUT_BROKER_ALT_PRESS || inEvent.inputEvent == INPUT_BROKER_UP_LONG) {
            curSelected--;
        } else if (inEvent.inputEvent == INPUT_BROKER_DOWN || inEvent.inputEvent == INPUT_BROKER_RIGHT ||
                   inEvent.inputEvent == INPUT_BROKER_USER_PRESS || inEvent.inputEvent == INPUT_BROKER_DOWN_LONG) {
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
    for (uint16_t i = 0; i < lineCount && i < visibleTotalLines; i++) {
        linePointers[i] = lineStarts[i];
    }

    uint8_t firstOptionToShow = 0;
    if (alertBannerOptions > 0) {
        if (visibleTotalLines - lineCount == 1) {
            firstOptionToShow = curSelected;
        } else if (curSelected > 1 && alertBannerOptions > visibleTotalLines - lineCount) {
            if (curSelected > alertBannerOptions - visibleTotalLines + lineCount)
                firstOptionToShow = alertBannerOptions - visibleTotalLines + lineCount;
            else
                firstOptionToShow = curSelected - 1;
        } else {
            firstOptionToShow = 0;
        }
    }
    // Useful log line for troubleshooting:
    /* LOG_WARN("alertBannerOptions: %u, curSelected: %u, visibleTotalLines: %u, lineCount: %u, firstOptionToShow: %u",
             alertBannerOptions, curSelected, visibleTotalLines, lineCount, firstOptionToShow); */

    for (int i = firstOptionToShow; i < alertBannerOptions && linesShown < visibleTotalLines; i++, linesShown++) {
        if (i == curSelected) {
            if (currentResolution == ScreenResolution::High) {
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
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    if (screen)
        screen->markTouchFrameMapped();
#endif

    bool is_picker = false;
    uint16_t lineCount = 0;
    // Layout Configuration
    constexpr uint16_t hPadding = 5;
    constexpr uint16_t vPadding = 2;
    bool needs_bell = false;
    uint16_t lineWidths[totalLines] = {0};
    uint16_t lineLengths[totalLines] = {0};
    BannerFont lineFonts[totalLines] = {};
    uint8_t lineEffectiveHeights[totalLines] = {0};
    const char *renderLines[totalLines] = {0};

    if (maxWidth != 0)
        is_picker = true;

    // Setup font and alignment
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // Track widest line INCLUDING bars (but don't change per-line widths)
    uint16_t widestLineWithBars = 0;

    while (lines[lineCount] != nullptr) {
        BannerFont lineFont = BANNER_FONT_DEFAULT;
        const char *renderText = resolveBannerLine(lineCount, lines[lineCount], lineFont);
        renderLines[lineCount] = renderText;
        lineFonts[lineCount] = lineFont;
        lineEffectiveHeights[lineCount] = effectiveLineHeightForBannerLine(lineFont);
        display->setFont(fontForBannerLine(lineFont));

        auto newlinePointer = strchr(renderText, '\n');
        if (newlinePointer)
            lineLengths[lineCount] = (newlinePointer - renderText);
        else
            lineLengths[lineCount] = strlen(renderText);

        if (current_notification_type == notificationTypeEnum::node_picker) {
            char measureBuffer[64] = {0};
            strncpy(measureBuffer, renderText, std::min<size_t>(lineLengths[lineCount], sizeof(measureBuffer) - 1));
            lineWidths[lineCount] = UIRenderer::measureStringWithEmotes(display, measureBuffer);
        } else {
            lineWidths[lineCount] = display->getStringWidth(renderText, lineLengths[lineCount], true);
        }

        // Consider extra width for signal bars on lines that contain "Signal:"
        uint16_t potentialWidth = lineWidths[lineCount];
        if (graphics::bannerSignalBars >= 0 && strncmp(renderText, "Signal:", 7) == 0) {
            const int totalBars = 5;
            const int barWidth = 3;
            const int barSpacing = 2;
            const int gap = 6; // space between text and bars
            int barsWidth = totalBars * barWidth + (totalBars - 1) * barSpacing + gap;
            potentialWidth += barsWidth;
        }

        if (potentialWidth > widestLineWithBars)
            widestLineWithBars = potentialWidth;

        if (!is_picker) {
            needs_bell |= (strstr(alertBannerMessage, "Alert Received") != nullptr);
            if (lineWidths[lineCount] > maxWidth)
                maxWidth = lineWidths[lineCount];
        }
        lineCount++;
    }
#if (defined(_VARIANT_T_DECK_PRO_V1_1) || defined(T_DECK_MAX) || T5S3_EPD_UI_PROFILE) && defined(USE_EINK)
    // Keep the legacy menu callbacks and touch values, but give T-Deck Pro menus
    // a self-contained Field console-style card so the frame underneath cannot show through.
    const bool isNodePicker = current_notification_type == notificationTypeEnum::node_picker;
    if (alertBannerOptions > 0 && (optionsArrayPtr != nullptr || isNodePicker) && lineCount > 0) {
        constexpr uint16_t menuWidth = T5S3_EPD_UI_MENU_WIDTH;
        constexpr uint16_t menuTitleHeight = T5S3_EPD_UI_MENU_TITLE_HEIGHT;
        constexpr uint16_t menuRowHeight = T5S3_EPD_UI_MENU_ROW_HEIGHT;
        constexpr uint16_t menuMessageRowHeight = T5S3_EPD_UI_MENU_MESSAGE_ROW_HEIGHT;
        constexpr uint16_t menuBottomPadding = T5S3_EPD_UI_MENU_BOTTOM_PADDING;
        constexpr uint16_t menuScreenMargin = T5S3_EPD_UI_MENU_SCREEN_MARGIN;

        const uint16_t screenWidth = display->getWidth();
        const uint16_t screenHeight = display->getHeight();
        const uint16_t boxWidth = std::min<uint16_t>(menuWidth, screenWidth > 12 ? screenWidth - 12 : screenWidth);
        const uint16_t menuOptionStartLine =
            (totalLines >= alertBannerOptions) ? totalLines - alertBannerOptions : totalLines;
        const uint16_t messageLineCount = std::min<uint16_t>(menuOptionStartLine, lineCount);
        const uint16_t messageRows = messageLineCount > 0 ? messageLineCount - 1 : 0;
        const int fixedHeight = menuTitleHeight + messageRows * menuMessageRowHeight + menuBottomPadding;
        const int availableHeight = std::max(1, static_cast<int>(screenHeight) - menuScreenMargin * 2);
        int maxOptionRows = (availableHeight - fixedHeight) / menuRowHeight;
        if (maxOptionRows < 1)
            maxOptionRows = 1;

        const uint16_t visibleOptionCount =
            std::min<uint16_t>(alertBannerOptions, static_cast<uint16_t>(maxOptionRows));
        const uint16_t boxHeight = static_cast<uint16_t>(fixedHeight + visibleOptionCount * menuRowHeight);
        const int16_t boxLeft = static_cast<int16_t>((screenWidth - boxWidth) / 2);
        int16_t boxTop = static_cast<int16_t>((static_cast<int>(screenHeight) - boxHeight) / 2);
        const int16_t minBoxTop = static_cast<int16_t>(menuScreenMargin / 2);
        const int16_t maxBoxTop = static_cast<int16_t>(screenHeight > boxHeight + minBoxTop
                                                            ? screenHeight - boxHeight - minBoxTop
                                                            : minBoxTop);
        if (boxTop < minBoxTop)
            boxTop = minBoxTop;
        if (boxTop > maxBoxTop)
            boxTop = maxBoxTop;

        // E-Ink color constants are inverted by the legacy display layer:
        // BLACK paints the paper and WHITE paints the ink.
        display->setColor(BLACK);
        display->fillRect(boxLeft, boxTop, boxWidth, boxHeight);
        display->setColor(WHITE);
        display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);

        char headerBuffer[64] = {0};
        const uint16_t headerLength = std::min<uint16_t>(lineLengths[0], sizeof(headerBuffer) - 1);
        memcpy(headerBuffer, renderLines[0], headerLength);
        headerBuffer[headerLength] = '\0';

        display->setColor(WHITE);
        display->fillRect(boxLeft + 1, boxTop + 1, boxWidth > 2 ? boxWidth - 2 : 1, menuTitleHeight - 1);
        display->setColor(BLACK);
        display->setFont(fontForBannerLine(lineFonts[0]));
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(boxLeft + 10, boxTop + 4, headerBuffer);

        const int16_t optionsTop = static_cast<int16_t>(boxTop + menuTitleHeight + messageRows * menuMessageRowHeight);
        for (uint16_t i = 1; i < messageLineCount; i++) {
            char messageBuffer[64] = {0};
            const uint16_t messageLength = std::min<uint16_t>(lineLengths[i], sizeof(messageBuffer) - 1);
            memcpy(messageBuffer, renderLines[i], messageLength);
            messageBuffer[messageLength] = '\0';
            display->setColor(WHITE);
            display->setFont(fontForBannerLine(lineFonts[i]));
            display->drawString(boxLeft + 10, boxTop + menuTitleHeight + (i - 1) * menuMessageRowHeight + 2,
                                messageBuffer);
        }

        const uint16_t maxFirstOption =
            alertBannerOptions > visibleOptionCount ? alertBannerOptions - visibleOptionCount : 0;
        uint16_t firstMenuOption = std::min<uint16_t>(firstOptionToShow, maxFirstOption);
        if (!isNodePicker && curSelected >= 0) {
            const uint16_t selected = static_cast<uint16_t>(curSelected);
            if (selected < firstMenuOption) {
                firstMenuOption = selected;
            } else if (selected >= firstMenuOption + visibleOptionCount) {
                firstMenuOption = selected - visibleOptionCount + 1;
            }
            if (firstMenuOption > maxFirstOption)
                firstMenuOption = maxFirstOption;
        }

        display->setColor(WHITE);
        display->drawLine(boxLeft + 7, optionsTop, boxLeft + boxWidth - 8, optionsTop);
        for (uint16_t row = 0; row < visibleOptionCount; row++) {
            const uint16_t optionIndex = firstMenuOption + row;
            const int16_t rowY = static_cast<int16_t>(optionsTop + row * menuRowHeight + 1);
            const bool selected = optionIndex == static_cast<uint16_t>(std::max<int8_t>(curSelected, 0));

            if (selected) {
                display->setColor(WHITE);
                display->fillRect(boxLeft + 6, rowY, boxWidth > 12 ? boxWidth - 12 : 1, menuRowHeight - 2);
            }

            display->setFont(FONT_SMALL);
            display->setColor(selected ? BLACK : WHITE);
            char optionNumber[4] = {0};
            int labelLeft = 12;
            if (!isNodePicker) {
                snprintf(optionNumber, sizeof(optionNumber), "%02u", static_cast<unsigned>(optionIndex + 1));
                display->drawString(boxLeft + 12, rowY + 5, optionNumber);
                labelLeft += display->getStringWidth(optionNumber) + display->getStringWidth("  ");
            }

            const uint16_t renderedOptionLine = static_cast<uint16_t>(messageLineCount + row);
            const char *optionText = "";
            if (isNodePicker) {
                if (renderedOptionLine < lineCount)
                    optionText = renderLines[renderedOptionLine];
            } else if (optionsArrayPtr[optionIndex]) {
                optionText = optionsArrayPtr[optionIndex];
            }
            char optionBuffer[64] = {0};
            const int labelMaxWidth = std::max(1, static_cast<int>(boxWidth) - labelLeft - 13);
            UIRenderer::truncateStringWithEmotes(display, optionText, optionBuffer, sizeof(optionBuffer), labelMaxWidth);
            display->drawString(boxLeft + labelLeft, rowY + 5, optionBuffer);

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
            if (screen) {
                screen->addTouchTarget(touchExpandedRect(boxLeft + 4, rowY, boxWidth - 8, menuRowHeight, 2),
                                       meshtastic::TouchTargetKind::NotificationOption, optionIndex,
                                       INPUT_BROKER_NONE);
            }
#endif

            display->setColor(WHITE);
            display->drawLine(boxLeft + 8, rowY + menuRowHeight - 1, boxLeft + boxWidth - 8,
                              rowY + menuRowHeight - 1);
        }

        if (alertBannerOptions > visibleOptionCount) {
            const int16_t scrollTrackX = static_cast<int16_t>(boxLeft + boxWidth - 5);
            const int16_t scrollTrackY = static_cast<int16_t>(optionsTop + 3);
            const int16_t scrollTrackHeight = static_cast<int16_t>(visibleOptionCount * menuRowHeight - 6);
            const int16_t thumbHeight =
                static_cast<int16_t>(std::max(8, (scrollTrackHeight * visibleOptionCount) / alertBannerOptions));
            const int16_t scrollRange = std::max<int16_t>(0, scrollTrackHeight - thumbHeight);
            const int16_t thumbY = static_cast<int16_t>(
                scrollTrackY + (maxFirstOption > 0 ? (scrollRange * firstMenuOption) / maxFirstOption : 0));
            display->setColor(WHITE);
            display->drawRect(scrollTrackX, scrollTrackY, 3, scrollTrackHeight);
            display->fillRect(scrollTrackX, thumbY, 3, thumbHeight);
        }

        display->setColor(BLACK);
        return;
    }
#endif

    // count lines

    // Ensure box accounts for signal bars if present
    if (widestLineWithBars > maxWidth)
        maxWidth = widestLineWithBars;

    uint16_t boxWidth = hPadding * 2 + maxWidth;

    if (needs_bell) {
        if ((currentResolution == ScreenResolution::High) && boxWidth <= 150)
            boxWidth += 26;
        if ((currentResolution == ScreenResolution::Low || currentResolution == ScreenResolution::UltraLow) && boxWidth <= 100)
            boxWidth += 20;
    }

    uint16_t screenHeight = display->height();
    uint8_t effectiveLineHeight = FONT_HEIGHT_SMALL - 3;
    uint8_t visibleTotalLines = 0;
    uint16_t contentHeight = 0;
    const uint16_t availableHeight = (screenHeight > (vPadding * 2)) ? (screenHeight - vPadding * 2) : 0;
    for (uint8_t i = 0; i < lineCount; i++) {
        uint8_t thisLineHeight = lineEffectiveHeights[i] ? lineEffectiveHeights[i] : effectiveLineHeight;
        if (contentHeight + thisLineHeight > availableHeight) {
            break;
        }
        contentHeight += thisLineHeight;
        visibleTotalLines++;
    }
    if (visibleTotalLines == 0 && lineCount > 0) {
        visibleTotalLines = 1;
        contentHeight = lineEffectiveHeights[0] ? lineEffectiveHeights[0] : effectiveLineHeight;
    }
    uint16_t boxHeight = contentHeight + vPadding * 2;
    if (visibleTotalLines == 1) {
        boxHeight += (currentResolution == ScreenResolution::High) ? 4 : 3;
    }

    int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    if (totalLines > visibleTotalLines) {
        boxWidth += (currentResolution == ScreenResolution::High) ? 4 : 2;
    }
    int16_t boxTop = (display->height() / 2) - (boxHeight / 2);
    boxHeight += (currentResolution == ScreenResolution::High) ? 2 : 1;
    if (graphics::isCompactPanel(display)) {
        boxLeft = 0;
        boxTop = 0;
        boxWidth = display->width();
        boxHeight = display->height();
    } else {
#if defined(OLED_TINY)
        if (visibleTotalLines == 1) {
            boxTop += 25;
        }
        if (alertBannerOptions < 3) {
            int missingLines = 3 - alertBannerOptions;
            int moveUp = missingLines * (effectiveLineHeight / 2);
            boxTop -= moveUp;
            if (boxTop < 0)
                boxTop = 0;
        }
#endif
    }

    // Draw Box
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
#if GRAPHICS_TFT_COLORING_ENABLED
    registerTFTActionMenuRegions(boxLeft, boxTop, boxWidth, boxHeight);
#endif

    // Draw Content
    int16_t lineY = boxTop + vPadding;
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    const uint16_t optionStartLine = (alertBannerOptions > 0 && totalLines >= alertBannerOptions)
                                         ? totalLines - alertBannerOptions
                                         : totalLines;
#endif
    for (int i = 0; i < visibleTotalLines; i++) {
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
        const int16_t rowY = lineY;
#endif
        display->setFont(fontForBannerLine(lineFonts[i]));
        int16_t thisLineHeight = lineEffectiveHeights[i] ? lineEffectiveHeights[i] : effectiveLineHeight;
        int16_t textX = boxLeft + (boxWidth - lineWidths[i]) / 2;
        if (needs_bell && i == 0) {
            int fontHeight = thisLineHeight + 3;
            int bellY = lineY + (fontHeight - 8) / 2;
            display->drawXbm(textX - 10, bellY, 8, 8, bell_alert);
            display->drawXbm(textX + lineWidths[i] + 2, bellY, 8, 8, bell_alert);
        }
        char lineBuffer[lineLengths[i] + 1];
        strncpy(lineBuffer, renderLines[i], lineLengths[i]);
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
            const int16_t titleBarY = boxTop + 1;
            const int16_t titleBarHeight = effectiveLineHeight - background_yOffset;
            display->fillRect(boxLeft, titleBarY, boxWidth, titleBarHeight);
#if GRAPHICS_TFT_COLORING_ENABLED
            if (alertBannerOptions > 0) {
                const uint16_t titleTextColor =
                    (getActiveTheme().id == ThemeID::DefaultLight) ? TFTPalette::Black : getThemeHeaderText();
                // Keep title role away from border/corner pixels so rounded-corner masks are not remapped to the title text
                // color.
                if (boxWidth > 2 && titleBarHeight > 0) {
                    setAndRegisterTFTColorRole(TFTColorRole::ActionMenuTitle, getThemeHeaderBg(), titleTextColor, boxLeft + 1,
                                               titleBarY, boxWidth - 2, titleBarHeight);
                }
            }
#endif
            display->setColor(BLACK);
            const int yOffset = graphics::isCompactPanel(display) ? 2 : 3;
            if (current_notification_type == notificationTypeEnum::node_picker) {
                UIRenderer::drawStringWithEmotes(display, textX, lineY - yOffset, lineBuffer, FONT_HEIGHT_SMALL, 1, false);
            } else {
                display->drawString(textX, lineY - yOffset, lineBuffer);
            }
            display->setColor(WHITE);
            lineY += (thisLineHeight - 2 - background_yOffset);
        } else {
            // Pop-up
            // If this is the Signal line, center text + bars as one group
            bool isSignalLine = (graphics::bannerSignalBars >= 0 && strstr(lineBuffer, "Signal:") != nullptr);
            if (isSignalLine) {
                const int totalBars = 5;
                const int barWidth = 3;
                const int barSpacing = 2;
                const int barHeightStep = 2;
                const int gap = 6;
                const int maxBarHeight = totalBars * barHeightStep;

                int textWidth = display->getStringWidth(lineBuffer, strlen(lineBuffer), true);
                int barsWidth = totalBars * barWidth + (totalBars - 1) * barSpacing + gap;
                int totalWidth = textWidth + barsWidth;
                int groupStartX = boxLeft + (boxWidth - totalWidth) / 2;

                if (current_notification_type == notificationTypeEnum::node_picker) {
                    UIRenderer::drawStringWithEmotes(display, groupStartX, lineY, lineBuffer, FONT_HEIGHT_SMALL, 1, false);
                } else {
                    display->drawString(groupStartX, lineY, lineBuffer);
                }

                int baseX = groupStartX + textWidth + gap;
                int baseY = lineY + effectiveLineHeight - 1;
#if GRAPHICS_TFT_COLORING_ENABLED
                if (graphics::bannerSignalBars > 0) {
                    uint16_t signalBarsColor = TFTPalette::Medium;
                    if (graphics::bannerSignalBars <= 1) {
                        signalBarsColor = TFTPalette::Bad;
                    } else if (graphics::bannerSignalBars >= 4) {
                        signalBarsColor = TFTPalette::Good;
                    }
                    const int activeBars = min(graphics::bannerSignalBars, totalBars);
                    const int regionWidth = activeBars * barWidth + (activeBars - 1) * barSpacing;
                    setAndRegisterTFTColorRole(TFTColorRole::SignalBars, signalBarsColor, TFTPalette::Black, baseX,
                                               baseY - maxBarHeight, regionWidth, maxBarHeight);
                }
#endif
                for (int b = 0; b < totalBars; b++) {
                    int barHeight = (b + 1) * barHeightStep;
                    int x = baseX + b * (barWidth + barSpacing);
                    int y = baseY - barHeight;

                    if (b < graphics::bannerSignalBars) {
                        display->fillRect(x, y, barWidth, barHeight);
                    } else {
                        display->drawRect(x, y, barWidth, barHeight);
                    }
                }
            } else {
                if (current_notification_type == notificationTypeEnum::node_picker) {
                    UIRenderer::drawStringWithEmotes(display, textX, lineY, lineBuffer, FONT_HEIGHT_SMALL, 1, false);
                } else {
                    display->drawString(textX, lineY, lineBuffer);
                }
            }
        }

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
        if (alertBannerOptions > 0 && i >= optionStartLine && screen) {
            const uint32_t optionIndex = static_cast<uint32_t>(firstOptionToShow + i - optionStartLine);
            if (optionIndex < alertBannerOptions) {
                screen->addTouchTarget(touchExpandedRect(boxLeft, rowY, boxWidth, thisLineHeight, 2),
                                       meshtastic::TouchTargetKind::NotificationOption, optionIndex,
                                       INPUT_BROKER_NONE);
            }
        }
        if (!(alertBannerOptions > 0 && i == 0))
            lineY += thisLineHeight;
#else
        lineY += thisLineHeight;
#endif
    }

    // Scroll Bar (Thicker, inside box, not over title)
    if (totalLines > visibleTotalLines) {
        const uint8_t scrollBarWidth = 5;
        int16_t scrollBarX = boxLeft + boxWidth - scrollBarWidth - 2;
        int16_t scrollBarY = boxTop + vPadding + effectiveLineHeight;
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

void NotificationRenderer::drawTextInput(OLEDDisplay *display, OLEDDisplayUiState *state)
{
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    auto &keyboardModule = OnScreenKeyboardModule::instance();
    const bool hasKeyboardTarget =
        inEvent.touchTargetKind == static_cast<uint8_t>(meshtastic::TouchTargetKind::KeyboardKey);
    if (inEvent.inputEvent != INPUT_BROKER_NONE || hasKeyboardTarget || inEvent.touchX != 0 || inEvent.touchY != 0) {
        const InputEvent event = inEvent;
        inEvent = {};
        keyboardModule.handleInput(event);
    }

    if (!keyboardModule.draw(display)) {
        if (current_notification_type == notificationTypeEnum::text_input)
            resetBanner();
        if (screen)
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    }
#else
    if (virtualKeyboard) {
        // Check for timeout and auto-exit if needed
        if (virtualKeyboard->isTimedOut()) {
            LOG_INFO("Virtual keyboard timeout - auto-exiting");
            // Cancel virtual keyboard - call callback with empty string to indicate timeout
            auto callback = textInputCallback; // Store callback before clearing

            // Clean up first to prevent re-entry. The keyboard belongs to OnScreenKeyboardModule; only stop()
            // may free it, and it clears virtualKeyboard/textInputCallback for us.
            OnScreenKeyboardModule::instance().stop(false);
            resetBanner();

            // Call callback after cleanup
            if (callback) {
                callback("");
            }

            // Restore normal overlays
            if (screen) {
                screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            }
            return;
        }

        if (inEvent.inputEvent != INPUT_BROKER_NONE) {
            bool handled = OnScreenKeyboardModule::processVirtualKeyboardInput(inEvent, virtualKeyboard);
            if (!handled && inEvent.inputEvent == INPUT_BROKER_CANCEL) {
                auto callback = textInputCallback;
                OnScreenKeyboardModule::instance().stop(false); // sole owner of the keyboard; also clears our aliases
                resetBanner();
                if (callback) {
                    callback("");
                }
                if (screen) {
                    screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
                }
                return;
            }

            // Consume the event after processing for virtual keyboard
            inEvent.inputEvent = INPUT_BROKER_NONE;
        }

        // Re-check pointer before drawing to avoid use-after-free and crashes
        if (!virtualKeyboard) {
            // Ensure we exit text_input state and restore frames
            if (current_notification_type == notificationTypeEnum::text_input) {
                resetBanner();
            }
            if (screen) {
                screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
            }
            // If screen is null, do nothing (safe fallback)
            return;
        }

        // Clear the screen to avoid overlapping with underlying frames or overlays
        display->setColor(BLACK);
        display->fillRect(0, 0, display->getWidth(), display->getHeight());
        display->setColor(WHITE);
        // Draw the virtual keyboard
        virtualKeyboard->draw(display, 0, 0);
    } else {
        // If virtualKeyboard is null, reset the banner to avoid getting stuck
        LOG_INFO("Virtual keyboard is null - resetting banner");
        resetBanner();
    }
#endif
}

bool NotificationRenderer::isOverlayBannerShowing()
{
    // Here 0 means "show indefinitely", so it must short-circuit the comparison.
    return strlen(alertBannerMessage) > 0 && (alertBannerUntil == 0 || !Throttle::deadlinePassed(alertBannerUntil));
}

bool NotificationRenderer::isMenuShowing()
{
    // A menu, picker, keyboard, or pairing-PIN overlay - anything interactive, as opposed to a plain
    // informational text banner (which has no options and type text_banner). Menus don't set a
    // notificationType of their own, so options are the only thing distinguishing them.
    return isOverlayBannerShowing() && (alertBannerOptions > 0 || current_notification_type != notificationTypeEnum::text_banner);
}

} // namespace graphics
#endif
