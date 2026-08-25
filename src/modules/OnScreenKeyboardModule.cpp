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

OnScreenKeyboardModule::~OnScreenKeyboardModule() = default;

void OnScreenKeyboardModule::start(const char *header, const char *initialText, uint32_t durationMs,
                                   std::function<void(const std::string &)> cb)
{
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    if (!t5Keyboard)
        t5Keyboard = std::make_unique<T5S3Keyboard>();
    callback = cb;
    t5Keyboard->start(header, initialText, durationMs, [this](const std::string &text) {
        if (text.empty()) {
            onCancel();
        } else {
            onSubmit(text);
        }
    });

    // The T5 keyboard has a different type and owns its own rendering path.
    NotificationRenderer::virtualKeyboard = nullptr;
#else
    keyboard = std::make_unique<VirtualKeyboard>();
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
    NotificationRenderer::virtualKeyboard = keyboard.get();
#endif
    NotificationRenderer::textInputCallback = callback;
}

void OnScreenKeyboardModule::stop(bool callEmptyCallback)
{
    auto cb = callback;
    callback = nullptr;
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    // Keep the object alive until the current draw/callback stack unwinds.
    if (t5Keyboard)
        t5Keyboard->stop(false);
#else
    keyboard.reset();
#endif
    // Keep NotificationRenderer legacy pointers in sync
    NotificationRenderer::virtualKeyboard = nullptr;
    NotificationRenderer::textInputCallback = nullptr;
    if (callEmptyCallback && cb)
        cb("");
}

bool OnScreenKeyboardModule::handleInput(const InputEvent &event)
{
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    return t5Keyboard && t5Keyboard->handleInput(event);
#else
    if (!keyboard)
        return false;

    if (processVirtualKeyboardInput(event, keyboard.get()))
        return true;

    if (event.inputEvent == INPUT_BROKER_CANCEL)
        onCancel();
    return false;
#endif
}

bool OnScreenKeyboardModule::isActive() const
{
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    return t5Keyboard && t5Keyboard->isActive();
#else
    return keyboard != nullptr;
#endif
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
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    if (!t5Keyboard)
        return false;
    if (t5Keyboard->isTimedOut()) {
        onCancel();
        return false;
    }
    return t5Keyboard->draw(display);
#else
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
    return true;
#endif
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

} // namespace graphics

#endif // HAS_SCREEN
