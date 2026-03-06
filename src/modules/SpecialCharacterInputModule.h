#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"
#include <string>

namespace graphics
{

class SpecialCharacterInputModule : public SingleButtonInputBase
{
  public:
    static SpecialCharacterInputModule &instance();

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
    SpecialCharacterInputModule();
    ~SpecialCharacterInputModule() = default;

    // Selection state
    enum SelectionLevel {
        LEVEL_BLOCK,    // Selecting which 3x3 block
        LEVEL_ROW,      // Selecting which row within a block
        LEVEL_CHARACTER // Selecting which character within a row
    };
    
    SelectionLevel currentLevel = LEVEL_BLOCK;
    int currentBlock = -1;      // 0-3 (blocks)
    int currentRow = -1;        // 0-2 (rows within block)
    int currentCharIndex = -1;  // 0-2 (characters within row)
    uint32_t lastPressTime = 0;
    
    // Constants
    static const uint32_t SELECTION_TIMEOUT_MS = 400;
    
    // Character layout: 4 blocks with 3 rows each
    static const char* BLOCK_CHARS[4][3];
    
    // Helper methods
    void advanceSelection();
    void confirmSelection();
    void resetToBlockLevel();
    int getBlockRowCount(int blockIndex) const;
    int getRowCharCount(int blockIndex, int rowIndex) const;
    char getCharAt(int blockIndex, int rowIndex, int charIndex) const;
    void addCharacterToInput(char c);
    
    // Drawing helpers
    void drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y);
    void drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height, bool highlighted);
};

} // namespace graphics

#endif
