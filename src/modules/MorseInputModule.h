#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"
#include <string>

namespace graphics
{

class MorseInputModule : public SingleButtonInputBase
{
  public:
    static MorseInputModule &instance();

    void start(const char *header, const char *initialText, uint32_t durationMs,
               std::function<void(const std::string &)> callback) override;
    
    void draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;

  protected:
    void handleButtonPress(uint32_t now) override;
    void handleButtonRelease(uint32_t now, uint32_t duration) override;
    void handleButtonHeld(uint32_t now, uint32_t duration) override;
    void handleIdle(uint32_t now) override;
    
    void handleMenuSelection(int selection) override;
    void drawInterface(OLEDDisplay *display, int16_t x, int16_t y) override;

  private:
    MorseInputModule();
    ~MorseInputModule() = default;

    // Morse state
    std::string currentMorse;
    uint32_t lastInputTime = 0;
    int consecutiveDots = 0;
    
    // Adaptive timing
    uint32_t dotDuration = 200; // Initial guess

    void commitCharacter();
    void processMorse();
    void updateTiming(uint32_t pressDuration);
    char morseToChar(const std::string &code);
    std::string charToMorse(char c);
    
    // Drawing helpers
    void drawMorseInterface(OLEDDisplay *display, int16_t x, int16_t y);
};

} // namespace graphics

#endif
