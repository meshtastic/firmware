#pragma once

#include "configuration.h"

#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

#include "T5S3KeyboardCore.h"
#include "graphics/Screen.h"
#include <OLEDDisplay.h>
#include <functional>
#include <stdint.h>
#include <string>

namespace graphics
{

class T5S3Keyboard
{
  public:
    using Callback = std::function<void(const std::string &)>;

    void start(const char *header, const char *initialText, uint32_t durationMs, Callback callback,
               const char *submitLabel = "Send");
    void stop(bool callEmptyCallback);

    bool handleInput(const InputEvent &event);
    bool draw(OLEDDisplay *display);
    bool isTimedOut() const;
    bool isActive() const { return active; }

  private:
    static constexpr uint16_t TEXT_CAPACITY = 201;
    static constexpr uint16_t HEADER_CAPACITY = 96;
    static constexpr uint16_t LABEL_CAPACITY = 24;
    static constexpr int16_t SCREEN_MARGIN = 16;
    static constexpr int16_t HEADER_HEIGHT = 52;
    static constexpr int16_t INPUT_TOP = 58;
    static constexpr int16_t INPUT_HEIGHT = 154;
    static constexpr int16_t KEYBOARD_TOP = 224;
    static constexpr int16_t KEYBOARD_HEIGHT = 576;
    static constexpr int16_t FOOTER_TOP = 820;
    static constexpr int16_t FOOTER_HEIGHT = 124;
    static constexpr int16_t KEY_GAP = 4;
    static constexpr uint32_t PRESSED_FEEDBACK_MS = 120;

    T5KeyboardState state{};
    T5KeyboardActionQueue pendingActions{};
    char text[TEXT_CAPACITY] = {};
    char header[HEADER_CAPACITY] = {};
    char submitLabel[LABEL_CAPACITY] = {};
    Callback callback;
    uint32_t timeoutMs = 0;
    uint32_t lastActivityMs = 0;
    uint32_t pressedUntilMs = 0;
    bool active = false;

    T5KeyboardResult activateKey(uint16_t keyId);
    void enqueueKey(uint16_t keyId);
    void drainActions();
    void finish(bool submitted);
    bool activateCharacter(char character);
    void drawInput(OLEDDisplay *display);
    void drawKey(OLEDDisplay *display, const T5KeyboardKey &key, int16_t x, int16_t y, int16_t width,
                 int16_t height);
    void drawFooter(OLEDDisplay *display);
    void drawLabelCentered(OLEDDisplay *display, const char *label, int16_t x, int16_t y, int16_t width,
                           int16_t height);
};

} // namespace graphics

#endif // T5S3_EPD_TOUCH_KEYBOARD && !MESHTASTIC_INCLUDE_NICHE_GRAPHICS
