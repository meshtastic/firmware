#pragma once

#include "configuration.h"
#if HAS_SCREEN

#include "graphics/Screen.h" // InputEvent
#include "graphics/VirtualKeyboard.h"
#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
#include "platform/extra_variants/t5s3_epaper/T5S3Keyboard.h"
#endif
#include <OLEDDisplay.h>
#include <functional>
#include <memory>
#include <string>

namespace graphics
{
class OnScreenKeyboardModule
{
  public:
    static OnScreenKeyboardModule &instance();

    void start(const char *header, const char *initialText, uint32_t durationMs,
               std::function<void(const std::string &)> callback);

    void stop(bool callEmptyCallback);

    bool handleInput(const InputEvent &event);
    bool isActive() const;
    static bool processVirtualKeyboardInput(const InputEvent &event, VirtualKeyboard *keyboard);
    bool draw(OLEDDisplay *display);

  private:
    OnScreenKeyboardModule() = default;
    ~OnScreenKeyboardModule();
    OnScreenKeyboardModule(const OnScreenKeyboardModule &) = delete;
    OnScreenKeyboardModule &operator=(const OnScreenKeyboardModule &) = delete;

    void onSubmit(const std::string &text);
    void onCancel();

#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    std::unique_ptr<T5S3Keyboard> t5Keyboard;
#else
    std::unique_ptr<VirtualKeyboard> keyboard;
#endif
    std::function<void(const std::string &)> callback;
};

} // namespace graphics

#endif // HAS_SCREEN
