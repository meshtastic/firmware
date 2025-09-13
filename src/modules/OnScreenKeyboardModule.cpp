#include "configuration.h"
#if HAS_SCREEN

#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/NotificationRenderer.h" // drawInvertedNotificationBox signature reuse
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

void OnScreenKeyboardModule::start(const char *header, const char *initialText, uint32_t,
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

bool OnScreenKeyboardModule::isActive() const
{
    return keyboard != nullptr;
}

void OnScreenKeyboardModule::handleInput(const InputEvent &event)
{
    if (!keyboard)
        return;

    // Auto-timeout check handled in draw() to centralize state transitions.
    switch (event.inputEvent) {
    case INPUT_BROKER_UP: {
        if (::rotaryEncoderInterruptImpl1 || ::upDownInterruptImpl1)
            keyboard->moveCursorLeft();
        else
            keyboard->moveCursorUp();
        break;
    }
    case INPUT_BROKER_DOWN: {
        if (::rotaryEncoderInterruptImpl1 || ::upDownInterruptImpl1)
            keyboard->moveCursorRight();
        else
            keyboard->moveCursorDown();
        break;
    }
    case INPUT_BROKER_LEFT:
        keyboard->moveCursorLeft();
        break;
    case INPUT_BROKER_RIGHT:
        keyboard->moveCursorRight();
        break;
    case INPUT_BROKER_UP_LONG:
        keyboard->moveCursorUp();
        break;
    case INPUT_BROKER_DOWN_LONG:
        keyboard->moveCursorDown();
        break;
    case INPUT_BROKER_ALT_PRESS:
        keyboard->moveCursorLeft();
        break;
    case INPUT_BROKER_USER_PRESS:
        keyboard->moveCursorRight();
        break;
    case INPUT_BROKER_SELECT:
        keyboard->handlePress();
        break;
    case INPUT_BROKER_SELECT_LONG:
        keyboard->handleLongPress();
        break;
    case INPUT_BROKER_CANCEL:
        onCancel();
        break;
    default:
        break;
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

    auto wrapText = [&](const char *text, uint16_t availableWidth) -> std::vector<std::string> {
        std::vector<std::string> wrapped;
        std::string current;
        std::string word;
        const char *p = text;
        while (*p && wrapped.size() < maxContentLines) {
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                if (*p == '\n') {
                    if (!current.empty()) {
                        wrapped.push_back(current);
                        current.clear();
                        if (wrapped.size() >= maxContentLines)
                            break;
                    }
                }
                ++p;
            }
            if (!*p || wrapped.size() >= maxContentLines)
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
                    wrapped.push_back(current);
                    current = word;
                    if (wrapped.size() >= maxContentLines)
                        break;
                } else {
                    current = word;
                    while (current.size() > 1 &&
                           display->getStringWidth(current.c_str(), current.length(), true) > availableWidth)
                        current.pop_back();
                }
            }
        }
        if (!current.empty() && wrapped.size() < maxContentLines)
            wrapped.push_back(current);
        return wrapped;
    };

    std::vector<std::string> allLines;
    if (hasTitle)
        allLines.emplace_back(popupTitle);

    char buf[sizeof(popupMessage)];
    strncpy(buf, popupMessage, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *paragraph = strtok(buf, "\n");
    while (paragraph && allLines.size() < maxContentLines + (hasTitle ? 1 : 0)) {
        auto wrapped = wrapText(paragraph, maxWrapWidth);
        for (const auto &ln : wrapped) {
            if (allLines.size() >= maxContentLines + (hasTitle ? 1 : 0))
                break;
            allLines.push_back(ln);
        }
        paragraph = strtok(nullptr, "\n");
    }

    std::vector<const char *> ptrs;
    for (const auto &ln : allLines)
        ptrs.push_back(ln.c_str());
    ptrs.push_back(nullptr);

    // Use the inverted notification box already present in NotificationRenderer
    NotificationRenderer::drawInvertedNotificationBox(display, nullptr, ptrs.data(), allLines.size(), 0, 0);
}

} // namespace graphics

#endif // HAS_SCREEN
