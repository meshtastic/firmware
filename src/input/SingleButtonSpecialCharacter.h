#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"

namespace graphics
{

class SingleButtonSpecialCharacter : public SingleButtonInputBase
{
  public:
    static SingleButtonSpecialCharacter &instance();

    void start(const char *header, const char *initialText, uint32_t durationMs,
               std::function<void(const std::string &)> callback) override;
    void draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;

  protected:
    void handleButtonPress(uint32_t now) override;
    void handleButtonRelease(uint32_t now, uint32_t duration) override;
    void handleIdle(uint32_t now) override;
    void handleMenuSelection(int selection) override;
    void drawInterface(OLEDDisplay *display, int16_t x, int16_t y) override;

  private:
    SingleButtonSpecialCharacter();
    ~SingleButtonSpecialCharacter() = default;

    enum SelectionLevel {
        LEVEL_BLOCK,
        LEVEL_ROW,
        LEVEL_CHARACTER
    };

    SelectionLevel currentLevel = LEVEL_BLOCK;
    int currentBlock = -1;
    int currentRow = -1;
    int currentCharIndex = -1;
    uint32_t lastPressTime = 0;

    static const uint32_t SELECTION_TIMEOUT_MS = 400;
    static const char *BLOCK_CHARS[4][3];

    void advanceSelection();
    void confirmSelection();
    void resetToBlockLevel();
    int getBlockRowCount(int blockIndex) const;
    int getRowCharCount(int blockIndex, int rowIndex) const;
    char getCharAt(int blockIndex, int rowIndex, int charIndex) const;
    void addCharacterToInput(char c);
    void drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y);
    void drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height, bool highlighted);
};

} // namespace graphics

#endif