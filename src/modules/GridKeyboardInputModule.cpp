#include "modules/GridKeyboardInputModule.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputManager.h"
#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

namespace graphics
{

// Character layout: 4 blocks with rows of characters
const char* GridKeyboardInputModule::BLOCK_CHARS[4][3] = {
    {"ABC", "DEF", "GHI"},     // Block 0: row 0, row 1, row 2
    {"JKL", "MNO", "PQR"},     // Block 1: row 0, row 1, row 2
    {"STU", "VWX", "YZ?"},     // Block 2: row 0, row 1, row 2
    {" ,.", "(?!", ");:"}      // Block 3: row 0 (single chars per row)
};

GridKeyboardInputModule &GridKeyboardInputModule::instance()
{
    static GridKeyboardInputModule inst;
    return inst;
}

GridKeyboardInputModule::GridKeyboardInputModule() : SingleButtonInputBase("GridKeyboard") {}

void GridKeyboardInputModule::start(const char *header, const char *initialText, uint32_t durationMs,
                                   std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header, initialText, durationMs, cb);
    
    // Reset to initial state
    resetToBlockLevel();
}

void GridKeyboardInputModule::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
    lastPressTime = now;
}

void GridKeyboardInputModule::handleButtonRelease(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        SingleButtonInputBase::handleButtonRelease(now, duration);
        return;
    }
    
    // Short press - advance selection
    if (duration < 2000) {
        advanceSelection();
        lastPressTime = now;
        
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void GridKeyboardInputModule::handleIdle(uint32_t now)
{
    if (menuOpen) {
        return;
    }
    
    // Check for selection timeout
    if (lastPressTime > 0 && (now - lastPressTime) >= SELECTION_TIMEOUT_MS) {
        confirmSelection();
        lastPressTime = 0;
        
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void GridKeyboardInputModule::advanceSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        if (currentBlock < 0) {
            currentBlock = 0;  // First press activates block 0
        } else {
            currentBlock++;
            if (currentBlock >= 4) {
                currentBlock = 0;  // Wrap around
            }
        }
        break;
        
    case LEVEL_COLUMN: {
        int colCount = getBlockColumnCount(currentBlock);
        if (currentColumn < 0) {
            currentColumn = 0;  // First press activates column 0
        } else {
            currentColumn++;
            if (currentColumn >= colCount) {
                // Reset to block selection (no active block)
                resetToBlockLevel();
            }
        }
        break;
    }
        
    case LEVEL_CHARACTER: {
        int charCount = getColumnCharCount(currentBlock, currentColumn);
        if (currentCharIndex < 0) {
            currentCharIndex = 0;  // First press activates character 0
        } else {
            currentCharIndex++;
            if (currentCharIndex >= charCount) {
                // Reset to block selection (no active block)
                resetToBlockLevel();
            }
        }
        break;
    }
    }
}

void GridKeyboardInputModule::confirmSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        // Only confirm if a block is selected
        if (currentBlock >= 0) {
            // All blocks now use column selection
            currentLevel = LEVEL_COLUMN;
            currentColumn = -1;  // Start inactive at column level
            currentCharIndex = -1;
        }
        break;
        
    case LEVEL_COLUMN:
        // Only confirm if a column is selected
        if (currentColumn >= 0) {
            // Move to character selection
            currentLevel = LEVEL_CHARACTER;
            currentCharIndex = -1;  // Start inactive at character level
        }
        break;
        
    case LEVEL_CHARACTER:
        // Only add character if one is selected
        if (currentCharIndex >= 0) {
            char c = getCharAt(currentBlock, currentColumn, currentCharIndex);
            addCharacterToInput(c);
            resetToBlockLevel();
        }
        break;
    }
}

void GridKeyboardInputModule::resetToBlockLevel()
{
    currentLevel = LEVEL_BLOCK;
    currentBlock = -1;  // Start inactive
    currentColumn = -1;
    currentCharIndex = -1;
    lastPressTime = 0;
}

int GridKeyboardInputModule::getBlockColumnCount(int blockIndex) const
{
    return 3; // All blocks have 3 columns
}

int GridKeyboardInputModule::getColumnCharCount(int blockIndex, int columnIndex) const
{
    const char* col = BLOCK_CHARS[blockIndex][columnIndex];
    return strlen(col);
}

char GridKeyboardInputModule::getCharAt(int blockIndex, int columnIndex, int charIndex) const
{
    const char* col = BLOCK_CHARS[blockIndex][columnIndex];
    if (charIndex >= 0 && charIndex < (int)strlen(col)) {
        return col[charIndex];
    }
    return 0;
}

void GridKeyboardInputModule::addCharacterToInput(char c)
{
    // Apply shift
    if (shift) {
        c = toupper(c);
        if (autoShift) shift = false;
    } else {
        c = tolower(c);
    }
    
    inputText += c;
    
    // Check for auto-shift
    if (c == '.' || c == '!' || c == '?') {
        shift = true;
    }
}

void GridKeyboardInputModule::handleMenuSelection(int selection)
{
    // Let base class handle all menu items
    SingleButtonInputBase::handleMenuSelection(selection);
}

void GridKeyboardInputModule::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    drawGridInterface(display, x, y);
}

void GridKeyboardInputModule::drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    int lineHeight = 10;
    int currentY = y;

    // Header
    display->drawString(x, currentY, headerText.c_str());
    currentY += lineHeight + 2;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 2;

    // Input Text with blinking cursor and scrolling
    std::string displayInput = getDisplayTextWithCursor();
    displayInput = formatDisplayTextWithScrolling(display, displayInput);

    display->drawString(x, currentY, displayInput.c_str());

    // Horizontal Line
    currentY += lineHeight;
    currentY += 3;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 3;

    // Grid layout
    // Each block is ~30 pixels wide, with 2px spacing between blocks
    int blockWidth = 30;
    int blockHeight = 24; // 3 rows of 8 pixels each
    int blockSpacing = 2;
    int startX = x + 2;
    
    // Draw the 4 blocks
    for (int b = 0; b < 4; b++) {
        int blockX = startX + b * (blockWidth + blockSpacing);
        bool isActiveBlock = (currentLevel == LEVEL_BLOCK && currentBlock == b);
        
        // Determine what to show based on selection level
        if (currentLevel == LEVEL_BLOCK) {
            // Show all blocks, highlight current one (only if currentBlock >= 0)
            drawBlock(display, b, blockX, currentY, blockWidth, blockHeight, isActiveBlock);
        } else if (currentBlock == b) {
            // This is the selected block - show it in more detail
            if (currentLevel == LEVEL_COLUMN) {
                // Show only the selected block's columns
                drawBlock(display, b, blockX, currentY, blockWidth, blockHeight, false);
            } else if (currentLevel == LEVEL_CHARACTER) {
                // Show only the selected column's characters
                drawBlock(display, b, blockX, currentY, blockWidth, blockHeight, false);
            }
        }
        // Non-selected blocks in drill-down modes are not drawn
    }
}

void GridKeyboardInputModule::drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height, bool highlighted)
{
    // Block outline (drawn when highlighted or at block level)
    if (highlighted) {
        display->fillRect(x - 1, y - 1, width + 2, height + 6);
        display->setColor(BLACK);
    }
    
    {
        // All blocks are 3x3 grids, displayed row by row
        int colWidth = width / 3;
        int rowHeight = height / 3;
        
        for (int row = 0; row < 3; row++) {
            const char* rowStr = BLOCK_CHARS[blockIndex][row];
            int rowY = y + row * rowHeight;
            
            // Row highlight (when in row selection mode)
            if (currentLevel == LEVEL_COLUMN && currentBlock == blockIndex && currentColumn == row) {
                display->fillRect(x, rowY, width, rowHeight + 6);  // +6 to fix height
                if (!highlighted) display->setColor(BLACK);
            }
            
            // Only show this row if we're in block level, column level (really row level) showing this row,
            // or character level showing this row
            bool showRow = (currentLevel == LEVEL_BLOCK) ||
                          (currentLevel == LEVEL_COLUMN && currentBlock == blockIndex) ||
                          (currentLevel == LEVEL_CHARACTER && currentBlock == blockIndex && currentColumn == row);
            
            if (showRow) {
                for (int col = 0; col < (int)strlen(rowStr) && col < 3; col++) {
                    char c = rowStr[col];
                    
                    // Apply shift for display
                    if (shift) {
                        c = toupper(c);
                    } else if (isalpha(c)) {
                        c = tolower(c);
                    }
                    
                    int colX = x + col * colWidth;
                    
                    // Character highlight (when in character selection mode)
                    bool isCharHighlighted = (currentLevel == LEVEL_CHARACTER && 
                                             currentBlock == blockIndex && 
                                             currentColumn == row && 
                                             currentCharIndex == col);
                    
                    if (isCharHighlighted) {
                        display->fillRect(colX, rowY, colWidth, rowHeight + 6);  // +6 to fix height
                        if (!highlighted) display->setColor(BLACK);
                    }
                    
                    char str[2] = {c, '\0'};
                    int textX = colX + colWidth / 2 - 3; // Center character
                    int textY = rowY + (rowHeight - 8) / 2; // Center vertically
                    display->drawString(textX, textY, str);
                    
                    if (isCharHighlighted && !highlighted) {
                        display->setColor(WHITE);
                    }
                }
            }
            
            // Reset color after row
            if (currentLevel == LEVEL_COLUMN && currentBlock == blockIndex && currentColumn == row && !highlighted) {
                display->setColor(WHITE);
            }
        }
    }
    
    // Reset color after block
    if (highlighted) {
        display->setColor(WHITE);
    }
}

void GridKeyboardInputModule::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!active)
        return;

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }

    drawInterface(display, x, y);
}

} // namespace graphics

#endif
