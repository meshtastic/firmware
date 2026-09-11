#include "configuration.h"
#if HAS_SCREEN

#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/NotificationRenderer.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "input/DeviceInputProvider.h"
#include "modules/OnScreenKeyboardModule.h"
#include "platform/DeviceVariant.h"
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
    if (auto *provider = getDeviceInputProvider(); provider && provider->supportsTextInput()) {
        keyboard.reset();
        providerTextInput = true;
        callback = cb;
        provider->startTextInput(header, initialText, durationMs, [this](const std::string &text) {
            if (text.empty())
                onCancel();
            else
                onSubmit(text);
        });
        NotificationRenderer::virtualKeyboard = nullptr;
        NotificationRenderer::textInputCallback = callback;
        return;
    }

    providerTextInput = false;
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
    NotificationRenderer::textInputCallback = callback;
}

void OnScreenKeyboardModule::stop(bool callEmptyCallback)
{
    auto cb = callback;
    callback = nullptr;
    if (providerTextInput) {
        providerTextInput = false;
        if (auto *provider = getDeviceInputProvider())
            provider->stopTextInput(false);
    } else {
        keyboard.reset();
    }
    // Keep NotificationRenderer legacy pointers in sync
    NotificationRenderer::virtualKeyboard = nullptr;
    NotificationRenderer::textInputCallback = nullptr;
    if (callEmptyCallback && cb)
        cb("");
}

bool OnScreenKeyboardModule::handleInput(const InputEvent &event)
{
    if (providerTextInput) {
        if (auto *provider = getDeviceInputProvider())
            return provider->handleTextInput(event);
        return false;
    }

    if (!keyboard)
        return false;

    if (processVirtualKeyboardInput(event, keyboard.get()))
        return true;

    if (event.inputEvent == INPUT_BROKER_CANCEL)
        onCancel();
    return false;
}

bool OnScreenKeyboardModule::isActive() const
{
    if (providerTextInput) {
        auto *provider = getDeviceInputProvider();
        return provider && provider->textInputIsActive();
    }
    return keyboard != nullptr;
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
    if (providerTextInput) {
        if (auto *provider = getDeviceInputProvider())
            return provider->drawTextInput(display);
        return false;
    }

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
