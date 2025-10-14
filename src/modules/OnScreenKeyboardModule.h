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

    void start(const char *header, const char *initialText,
               uint32_t,
               std::function<void(const std::string &)> callback);

    void stop(bool callEmptyCallback);


    void handleInput(const InputEvent &event);
    bool draw(OLEDDisplay *display);

    void showPopup(const char *title, const char *content, uint32_t durationMs);
    void clearPopup();
  // Draw only the popup overlay (used when legacy virtualKeyboard draws the keyboard)
  void drawPopupOverlay(OLEDDisplay *display);

  private:
    OnScreenKeyboardModule() = default;
    ~OnScreenKeyboardModule();
    OnScreenKeyboardModule(const OnScreenKeyboardModule &) = delete;
    OnScreenKeyboardModule &operator=(const OnScreenKeyboardModule &) = delete;

    void onSubmit(const std::string &text);
    void onCancel();

    void drawPopup(OLEDDisplay *display);

    VirtualKeyboard *keyboard = nullptr;
    std::function<void(const std::string &)> callback;

    char popupTitle[64] = {0};
    char popupMessage[256] = {0};
    uint32_t popupUntil = 0;
    bool popupVisible = false;
};

} // namespace graphics

#endif // HAS_SCREEN
