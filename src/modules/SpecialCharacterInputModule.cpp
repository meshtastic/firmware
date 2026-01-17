#include "modules/SpecialCharacterInputModule.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputManager.h"
#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

namespace graphics
{

// Character layout: 4 blocks with rows of characters
const char* SpecialCharacterInputModule::BLOCK_CHARS[4][3] = {
    {"123", "456", "789"},     // Block 0: numbers
    {"0?!", "()/", "\\[]"},    // Block 1: punctuation & brackets
    {",.'", ";:\"", "+-*"},    // Block 2: punctuation & math
    {"!@#", "$\%|", "&*="}      // Block 3: symbols
};

SpecialCharacterInputModule &SpecialCharacterInputModule::instance()
{
    static SpecialCharacterInputModule inst;
    return inst;
}

SpecialCharacterInputModule::SpecialCharacterInputModule() : SingleButtonInputBase("SpecialChars") {}

void SpecialCharacterInputModule::start(const char *header, const char *initialText, uint32_t durationMs,
                                       std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header ? header : "Special Characters", initialText, durationMs, cb);
    
    // Reset to initial state
    resetToBlockLevel();
}

void SpecialCharacterInputModule::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
    lastPressTime = now;
}

void SpecialCharacterInputModule::handleButtonRelease(uint32_t now, uint32_t duration)
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

void SpecialCharacterInputModule::handleButtonHeld(uint32_t now, uint32_t duration)
{
    // Long press (≥2s) opens menu
    if (duration >= 2000) {
        SingleButtonInputBase::handleButtonHeld(now, duration);
    }
}

void SpecialCharacterInputModule::handleIdle(uint32_t now)
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

void SpecialCharacterInputModule::advanceSelection()
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
        
    case LEVEL_ROW: {
        int rowCount = getBlockRowCount(currentBlock);
        if (currentRow < 0) {
            currentRow = 0;  // First press activates row 0
        } else {
            currentRow++;
            if (currentRow >= rowCount) {
                // Reset to block selection (no active block)
                resetToBlockLevel();
            }
        }
        break;
    }
        
    case LEVEL_CHARACTER: {
        int charCount = getRowCharCount(currentBlock, currentRow);
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

void SpecialCharacterInputModule::confirmSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        // Only confirm if a block is selected
        if (currentBlock >= 0) {
            currentLevel = LEVEL_ROW;
            currentRow = -1;  // Start inactive at row level
            currentCharIndex = -1;
        }
        break;
        
    case LEVEL_ROW:
        // Only confirm if a row is selected
        if (currentRow >= 0) {
            currentLevel = LEVEL_CHARACTER;
            currentCharIndex = -1;  // Start inactive at character level
        }
        break;
        
    case LEVEL_CHARACTER:
        // Only add character if one is selected
        if (currentCharIndex >= 0) {
            char c = getCharAt(currentBlock, currentRow, currentCharIndex);
            addCharacterToInput(c);
            resetToBlockLevel();
        }
        break;
    }
}

void SpecialCharacterInputModule::resetToBlockLevel()
{
    currentLevel = LEVEL_BLOCK;
    currentBlock = -1;  // Start inactive
    currentRow = -1;
    currentCharIndex = -1;
    lastPressTime = 0;
}

int SpecialCharacterInputModule::getBlockRowCount(int blockIndex) const
{
    return 3; // All blocks have 3 rows
}

int SpecialCharacterInputModule::getRowCharCount(int blockIndex, int rowIndex) const
{
    const char* row = BLOCK_CHARS[blockIndex][rowIndex];
    return strlen(row);
}

char SpecialCharacterInputModule::getCharAt(int blockIndex, int rowIndex, int charIndex) const
{
    const char* row = BLOCK_CHARS[blockIndex][rowIndex];
    if (charIndex >= 0 && charIndex < (int)strlen(row)) {
        return row[charIndex];
    }
    return 0;
}

void SpecialCharacterInputModule::addCharacterToInput(char c)
{
    inputText += c;
    
    // Auto-shift logic after certain punctuation
    if (c == '.' || c == '!' || c == '?') {
        shift = true;
    }
}

void SpecialCharacterInputModule::handleModeSwitch(int modeIndex)
{
    std::string savedText = inputText;
    auto savedCallback = callback;
    std::string savedHeader = headerText;
    
    // Stop this module without calling callback
    stop(false);
    
    // Switch mode based on index
    if (modeIndex == 0) { // Morse
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_MORSE);
    } else if (modeIndex == 1) { // Grid Keyboard
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_GRID_KEYBOARD);
    } else if (modeIndex == 2) { // Special Characters (current mode)
        // Already in Special Characters mode, just close menu
        menuOpen = false;
        inputModeMenuOpen = false;
        return;
    }
    
    // Start the new module with saved state
    SingleButtonInputManager::instance().start(savedHeader.c_str(), savedText.c_str(), 0, savedCallback);
}

void SpecialCharacterInputModule::handleMenuSelection(int selection)
{
    // Let base class handle all menu items
    SingleButtonInputBase::handleMenuSelection(selection);
}

void SpecialCharacterInputModule::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    drawGridInterface(display, x, y);
}

void SpecialCharacterInputModule::drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y)
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

    // Input Text with blinking cursor
    std::string displayInput = inputText;
    if ((millis() / 500) % 2 == 0) {
        displayInput += "_";
    }

    // Handle scrolling if text is too long
    int width = display->getStringWidth(displayInput.c_str());
    int maxWidth = display->getWidth();
    if (width > maxWidth) {
        int charWidth = 6;
        int maxChars = maxWidth / charWidth;
        if (displayInput.length() > (size_t)maxChars) {
            displayInput = "..." + displayInput.substr(displayInput.length() - maxChars + 3);
        }
    }

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
            if (currentLevel == LEVEL_ROW) {
                // Show only the selected block's rows
                drawBlock(display, b, blockX, currentY, blockWidth, blockHeight, false);
            } else if (currentLevel == LEVEL_CHARACTER) {
                // Show only the selected row's characters
                drawBlock(display, b, blockX, currentY, blockWidth, blockHeight, false);
            }
        }
        // Non-selected blocks in drill-down modes are not drawn
    }
}

void SpecialCharacterInputModule::drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height, bool highlighted)
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
            if (currentLevel == LEVEL_ROW && currentBlock == blockIndex && currentRow == row) {
                display->fillRect(x, rowY, width, rowHeight + 6);  // +6 to fix height
                if (!highlighted) display->setColor(BLACK);
            }
            
            // Only show this row if we're in block level, row level showing this row,
            // or character level showing this row
            bool showRow = (currentLevel == LEVEL_BLOCK) ||
                          (currentLevel == LEVEL_ROW && currentBlock == blockIndex) ||
                          (currentLevel == LEVEL_CHARACTER && currentBlock == blockIndex && currentRow == row);
            
            if (showRow) {
                for (int col = 0; col < (int)strlen(rowStr) && col < 3; col++) {
                    char c = rowStr[col];
                    
                    int colX = x + col * colWidth;
                    
                    // Character highlight (when in character selection mode)
                    bool isCharHighlighted = (currentLevel == LEVEL_CHARACTER && 
                                             currentBlock == blockIndex && 
                                             currentRow == row && 
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
            if (currentLevel == LEVEL_ROW && currentBlock == blockIndex && currentRow == row && !highlighted) {
                display->setColor(WHITE);
            }
        }
    }
    
    // Reset color after block
    if (highlighted) {
        display->setColor(WHITE);
    }
}

void SpecialCharacterInputModule::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
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
