#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"

namespace graphics
{

class SingleButtonGridKeyboard : public SingleButtonInputBase
{
  public:
    static SingleButtonGridKeyboard &instance();

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
    SingleButtonGridKeyboard();
    ~SingleButtonGridKeyboard() = default;

    enum SelectionLevel {
        LEVEL_BLOCK,
        LEVEL_COLUMN,
        LEVEL_CHARACTER
    };

    SelectionLevel currentLevel = LEVEL_BLOCK;
    int currentBlock = -1;
    int currentColumn = -1;
    int currentCharIndex = -1;
    uint32_t lastPressTime = 0;

    static const uint32_t SELECTION_TIMEOUT_MS = 400;
    static const char *BLOCK_CHARS[4][3];

    void advanceSelection();
    void confirmSelection();
    void resetToBlockLevel();
    int getBlockColumnCount(int blockIndex) const;
    int getColumnCharCount(int blockIndex, int columnIndex) const;
    char getCharAt(int blockIndex, int columnIndex, int charIndex) const;
    void addCharacterToInput(char c);
    void drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y);
    void drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height, bool highlighted);
};

} // namespace graphics

#endif