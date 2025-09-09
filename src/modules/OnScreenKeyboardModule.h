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

// Lightweight UI module to manage on-screen keyboard (non-touch).
class OnScreenKeyboardModule
{
  public:
    static OnScreenKeyboardModule &instance();

    // Begin a keyboard session
    void start(const char *header, const char *initialText,
               uint32_t /*durationMs unused here - NotificationRenderer controls banner timeout*/,
               std::function<void(const std::string &)> callback);

    // Stop current session (optionally call callback with empty string)
    void stop(bool callEmptyCallback);

    // Session status
    bool isActive() const;

    // Event handling + drawing
    void handleInput(const InputEvent &event);
    // Draw keyboard and any overlay popup; return false if session ended (timeout or submit/cancel)
    bool draw(OLEDDisplay *display);

    // Popup helpers (title/content shown above keyboard)
    void showPopup(const char *title, const char *content, uint32_t durationMs);
    void clearPopup();

    // Compatibility: expose underlying keyboard pointer for existing callsites
    VirtualKeyboard *getKeyboard() const { return keyboard; }

  private:
    OnScreenKeyboardModule() = default;
    ~OnScreenKeyboardModule();
    OnScreenKeyboardModule(const OnScreenKeyboardModule &) = delete;
    OnScreenKeyboardModule &operator=(const OnScreenKeyboardModule &) = delete;

    // Internal helpers
    void onSubmit(const std::string &text);
    void onCancel();

    // Popup rendering
    void drawPopup(OLEDDisplay *display);

    VirtualKeyboard *keyboard = nullptr;
    std::function<void(const std::string &)> callback;

    // Popup state
    char popupTitle[64] = {0};
    char popupMessage[256] = {0};
    uint32_t popupUntil = 0;
    bool popupVisible = false;
};

} // namespace graphics

#endif // HAS_SCREEN
