#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/Screen.h"
#include "input/InputBroker.h"
#include "concurrency/OSThread.h"
#include <functional>
#include <string>

namespace graphics
{

class MorseInputModule : public concurrency::OSThread, public Observable<const UIFrameEvent *>
{
  public:
    static MorseInputModule &instance();

    void start(const char *header, const char *initialText, uint32_t durationMs,
               std::function<void(const std::string &)> callback);
    void stop(bool callEmptyCallback = false);

    // Returns true if the event was consumed
    bool handleInput(const InputEvent &event);
    
    void draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    bool isActive() const { return active; }

  protected:
    int32_t runOnce() override;

  private:
    MorseInputModule();
    ~MorseInputModule() = default;

    bool active = false;
    std::string headerText;
    std::string inputText;
    std::function<void(const std::string &)> callback;

    // Morse state
    std::string currentMorse;
    uint32_t lastInputTime = 0;
    uint32_t buttonPressTime = 0;
    bool buttonPressed = false;
    bool ignoreRelease = false;
    bool waitForRelease = false;
    bool shift = false;
    int consecutiveDots = 0;
    bool autoShift = true;
    
    // Adaptive timing
    uint32_t dotDuration = 200; // Initial guess
    
    // Menu state
    bool menuOpen = false;
    int menuSelection = 0;
    
    // Char picker state
    bool charPickerOpen = false;
    int charPickerSelection = 0;

    void commitCharacter();
    void processMorse();
    void updateTiming(uint32_t pressDuration);
    char morseToChar(const std::string &code);
    std::string charToMorse(char c);
    
    // Drawing helpers
    void drawMorseInterface(OLEDDisplay *display, int16_t x, int16_t y);
    void drawMenu(OLEDDisplay *display, int16_t x, int16_t y);
    void drawCharPicker(OLEDDisplay *display, int16_t x, int16_t y);
};

} // namespace graphics

#endif
