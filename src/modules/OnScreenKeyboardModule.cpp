#include "configuration.h"
#if HAS_SCREEN

#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/NotificationRenderer.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "modules/OnScreenKeyboardModule.h"
#include <Arduino.h>
#include <algorithm>

namespace graphics
{

OnScreenKeyboardModule &OnScreenKeyboardModule::instance()
{
    static OnScreenKeyboardModule inst;
    return inst;
}

OnScreenKeyboardModule::~OnScreenKeyboardModule()
{
    if (keyboard) {
        delete keyboard;
        keyboard = nullptr;
    }
}

void OnScreenKeyboardModule::start(const char *header, const char *initialText, uint32_t durationMs,
                                   std::function<void(const std::string &)> cb)
{
    if (keyboard) {
        delete keyboard;
        keyboard = nullptr;
    }
    keyboard = new VirtualKeyboard();
    callback = cb;
    if (header)
        keyboard->setHeader(header);
    if (initialText)
        keyboard->setInputText(initialText);

    // Route VK submission/cancel events back into the module
    keyboard->setCallback([this](const std::string &text) {
        if (text.empty()) {
            this->onCancel();
        } else {
            this->onSubmit(text);
        }
    });

    // Maintain legacy compatibility hooks
    NotificationRenderer::virtualKeyboard = keyboard;
    NotificationRenderer::textInputCallback = callback;
}

void OnScreenKeyboardModule::stop(bool callEmptyCallback)
{
    auto cb = callback;
    callback = nullptr;
    if (keyboard) {
        delete keyboard;
        keyboard = nullptr;
    }
    // Keep NotificationRenderer legacy pointers in sync
    NotificationRenderer::virtualKeyboard = nullptr;
    NotificationRenderer::textInputCallback = nullptr;
    clearPopup();
    if (callEmptyCallback && cb)
        cb("");
}

void OnScreenKeyboardModule::handleInput(const InputEvent &event)
{
    if (!keyboard)
        return;

    if (processVirtualKeyboardInput(event, keyboard))
        return;

    if (event.inputEvent == INPUT_BROKER_CANCEL)
        onCancel();
}

bool OnScreenKeyboardModule::processVirtualKeyboardInput(const InputEvent &event, VirtualKeyboard *targetKeyboard)
{
    if (!targetKeyboard)
        return false;

    switch (event.inputEvent) {
    case INPUT_BROKER_UP:
    case INPUT_BROKER_UP_LONG:
        targetKeyboard->moveCursorUp();
        return true;
    case INPUT_BROKER_DOWN:
    case INPUT_BROKER_DOWN_LONG:
        targetKeyboard->moveCursorDown();
        return true;
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_ALT_PRESS:
        targetKeyboard->moveCursorLeft();
        return true;
    case INPUT_BROKER_RIGHT:
    case INPUT_BROKER_USER_PRESS:
        targetKeyboard->moveCursorRight();
        return true;
    case INPUT_BROKER_SELECT:
        targetKeyboard->handlePress();
        return true;
    case INPUT_BROKER_SELECT_LONG:
        targetKeyboard->handleLongPress();
        return true;
    default:
        return false;
    }
}

bool OnScreenKeyboardModule::draw(OLEDDisplay *display)
{
    if (!keyboard)
        return false;

    // Timeout
    if (keyboard->isTimedOut()) {
        onCancel();
        return false;
    }

    // Clear full screen behind keyboard
    display->setColor(BLACK);
    display->fillRect(0, 0, display->getWidth(), display->getHeight());
    display->setColor(WHITE);
    keyboard->draw(display, 0, 0);

    // Draw popup overlay if needed
    drawPopup(display);
    return true;
}

void OnScreenKeyboardModule::onSubmit(const std::string &text)
{
    auto cb = callback;
    stop(false);
    if (cb)
        cb(text);
}

void OnScreenKeyboardModule::onCancel()
{
    stop(true);
}

void OnScreenKeyboardModule::showPopup(const char *title, const char *content, uint32_t durationMs)
{
    if (!title || !content)
        return;
    strncpy(popupTitle, title, sizeof(popupTitle) - 1);
    popupTitle[sizeof(popupTitle) - 1] = '\0';
    strncpy(popupMessage, content, sizeof(popupMessage) - 1);
    popupMessage[sizeof(popupMessage) - 1] = '\0';
    popupUntil = millis() + durationMs;
    popupVisible = true;
}

void OnScreenKeyboardModule::clearPopup()
{
    popupTitle[0] = '\0';
    popupMessage[0] = '\0';
    popupUntil = 0;
    popupVisible = false;
}

void OnScreenKeyboardModule::drawPopupOverlay(OLEDDisplay *display)
{
    // Only render the popup overlay (without drawing the keyboard)
    drawPopup(display);
}

void OnScreenKeyboardModule::drawPopup(OLEDDisplay *display)
{
    if (!popupVisible)
        return;
    if (millis() > popupUntil || popupMessage[0] == '\0') {
        popupVisible = false;
        return;
    }

    // Build lines and leverage NotificationRenderer inverted box drawing for consistent style
    constexpr uint16_t maxContentLines = 3;
    const bool hasTitle = popupTitle[0] != '\0';

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const uint16_t maxWrapWidth = display->width() - 40;

    char lineStorage[maxContentLines + 1][64] = {};
    const char *linePtrs[maxContentLines + 2] = {};
    uint8_t lineCount = 0;

    auto appendLine = [&](const std::string &line) {
        if (line.empty() || lineCount >= maxContentLines + (hasTitle ? 1 : 0))
            return;
        strncpy(lineStorage[lineCount], line.c_str(), sizeof(lineStorage[lineCount]) - 1);
        lineStorage[lineCount][sizeof(lineStorage[lineCount]) - 1] = '\0';
        linePtrs[lineCount] = lineStorage[lineCount];
        ++lineCount;
    };

    auto wrapText = [&](const char *text, uint16_t availableWidth) {
        std::string current;
        std::string word;
        const char *p = text;
        while (*p && lineCount < maxContentLines + (hasTitle ? 1 : 0)) {
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                if (*p == '\n') {
                    if (!current.empty()) {
                        appendLine(current);
                        current.clear();
                        if (lineCount >= maxContentLines + (hasTitle ? 1 : 0))
                            break;
                    }
                }
                ++p;
            }
            if (!*p || lineCount >= maxContentLines + (hasTitle ? 1 : 0))
                break;
            word.clear();
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                word += *p++;
            if (word.empty())
                continue;
            std::string test = current.empty() ? word : (current + " " + word);
            uint16_t w = display->getStringWidth(test.c_str(), test.length(), true);
            if (w <= availableWidth)
                current = test;
            else {
                if (!current.empty()) {
                    appendLine(current);
                    current = word;
                    if (lineCount >= maxContentLines + (hasTitle ? 1 : 0))
                        break;
                } else {
                    current = word;
                    while (current.size() > 1 &&
                           display->getStringWidth(current.c_str(), current.length(), true) > availableWidth)
                        current.pop_back();
                }
            }
        }
        if (!current.empty() && lineCount < maxContentLines + (hasTitle ? 1 : 0))
            appendLine(current);
    };

    if (hasTitle)
        appendLine(popupTitle);

    char buf[sizeof(popupMessage)];
    strncpy(buf, popupMessage, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *paragraph = strtok(buf, "\n");
    while (paragraph && lineCount < maxContentLines + (hasTitle ? 1 : 0)) {
        wrapText(paragraph, maxWrapWidth);
        paragraph = strtok(nullptr, "\n");
    }

    linePtrs[lineCount] = nullptr;

    // Use the standard notification box drawing from NotificationRenderer
    NotificationRenderer::drawNotificationBox(display, nullptr, linePtrs, lineCount, 0, 0);
}

} // namespace graphics

#endif // HAS_SCREEN
