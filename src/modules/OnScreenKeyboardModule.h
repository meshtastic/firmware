#pragma once

#include "configuration.h"
#if HAS_SCREEN

#include "graphics/Screen.h" // InputEvent
#include "graphics/VirtualKeyboard.h"
#include <OLEDDisplay.h>
#include <functional>
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

    void handleInput(const InputEvent &event);
    static bool processVirtualKeyboardInput(const InputEvent &event, VirtualKeyboard *keyboard);
    bool draw(OLEDDisplay *display);

  private:
    OnScreenKeyboardModule() = default;
    ~OnScreenKeyboardModule();
    OnScreenKeyboardModule(const OnScreenKeyboardModule &) = delete;
    OnScreenKeyboardModule &operator=(const OnScreenKeyboardModule &) = delete;

    void onSubmit(const std::string &text);
    void onCancel();

    VirtualKeyboard *keyboard = nullptr;
    std::function<void(const std::string &)> callback;
};

} // namespace graphics

#endif // HAS_SCREEN
