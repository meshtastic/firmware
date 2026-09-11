#pragma once

#include "InputBroker.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class TCA8418KeyboardBase;
class OLEDDisplay;

// Variant-owned input hardware is initialized through this boundary. Common
// input code consumes the resulting sources and keyboard adapter only.
class DeviceInputProvider
{
  public:
    virtual ~DeviceInputProvider() = default;

    virtual void begin() {}
    virtual void shutdown() {}
    virtual bool usesGestureRecognizer() const { return false; }
    virtual std::unique_ptr<TCA8418KeyboardBase> createTca8418Keyboard();
    virtual uint8_t tca8418KeyboardAddress() const { return 0x34; }
    virtual bool isMaxTouchKeySource(const char *source) const
    {
        (void)source;
        return false;
    }
    virtual bool isSafeMenuBackLabel(const char *label) const
    {
        (void)label;
        return false;
    }

    // Optional device-owned text input surface. The default implementation keeps
    // the existing VirtualKeyboard path for boards without a touch keyboard.
    virtual bool supportsTextInput() const { return false; }
    virtual void startTextInput(const char *header, const char *initialText, uint32_t durationMs,
                                std::function<void(const std::string &)> callback)
    {
        (void)header;
        (void)initialText;
        (void)durationMs;
        (void)callback;
    }
    virtual void stopTextInput(bool callEmptyCallback) { (void)callEmptyCallback; }
    virtual bool handleTextInput(const InputEvent &event)
    {
        (void)event;
        return false;
    }
    virtual bool drawTextInput(OLEDDisplay *display)
    {
        (void)display;
        return false;
    }
    virtual bool textInputIsActive() const { return false; }
};
