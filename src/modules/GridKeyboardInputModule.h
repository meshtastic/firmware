#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"
#include <string>

namespace graphics
{

class GridKeyboardInputModule : public SingleButtonInputBase
{
  public:
    static GridKeyboardInputModule &instance();

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
    GridKeyboardInputModule();
    ~GridKeyboardInputModule() = default;

    // Selection state
    enum SelectionLevel {
        LEVEL_BLOCK,    // Selecting which 3x3 block
        LEVEL_COLUMN,   // Selecting which column within a block
        LEVEL_CHARACTER // Selecting which character within a column
    };
    
    SelectionLevel currentLevel = LEVEL_BLOCK;
    int currentBlock = 0;      // 0-3 (blocks)
    int currentColumn = 0;     // 0-2 (columns within block)
    int currentCharIndex = 0;  // 0-2 (characters within column)
    uint32_t lastPressTime = 0;
    
    // Constants
    static const uint32_t SELECTION_TIMEOUT_MS = 400;
    
    // Character layout
    static const char* BLOCK_CHARS[4][3];  // 4 blocks, each with up to 3 columns
    
    // Helper methods
    void advanceSelection();
    void confirmSelection();
    void resetToBlockLevel();
    int getBlockColumnCount(int blockIndex) const;
    int getColumnCharCount(int blockIndex, int columnIndex) const;
    char getCharAt(int blockIndex, int columnIndex, int charIndex) const;
    void addCharacterToInput(char c);
    
    // Drawing helpers
    void drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y);
    void drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height, bool highlighted);
};

} // namespace graphics

#endif
