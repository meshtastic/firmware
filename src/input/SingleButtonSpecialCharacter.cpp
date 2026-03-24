#include "input/SingleButtonSpecialCharacter.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

namespace graphics
{

const char *SingleButtonSpecialCharacter::BLOCK_CHARS[4][3] = {
    {"123", "456", "789"},
    {"0?!", "()/", "\\[]"},
    {",.'", ";:\"", "+-*"},
    {"!@#", "$%|", "&*="}
};

SingleButtonSpecialCharacter &SingleButtonSpecialCharacter::instance()
{
    static SingleButtonSpecialCharacter inst;
    return inst;
}

SingleButtonSpecialCharacter::SingleButtonSpecialCharacter() : SingleButtonInputBase("SingleButtonSpecialCharacter") {}

void SingleButtonSpecialCharacter::start(const char *header, const char *initialText, uint32_t durationMs,
                                         std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header ? header : "Special Characters", initialText, durationMs, cb);
    resetToBlockLevel();
}

void SingleButtonSpecialCharacter::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
    lastPressTime = now;
}

void SingleButtonSpecialCharacter::handleButtonRelease(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        SingleButtonInputBase::handleButtonRelease(now, duration);
        return;
    }

    if (duration < 2000) {
        advanceSelection();
        lastPressTime = now;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void SingleButtonSpecialCharacter::handleIdle(uint32_t now)
{
    if (menuOpen) {
        return;
    }

    if (lastPressTime > 0 && (now - lastPressTime) >= SELECTION_TIMEOUT_MS) {
        confirmSelection();
        lastPressTime = 0;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void SingleButtonSpecialCharacter::advanceSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        if (currentBlock < 0) {
            currentBlock = 0;
        } else if (++currentBlock >= 4) {
            currentBlock = 0;
        }
        break;

    case LEVEL_ROW: {
        int rowCount = getBlockRowCount(currentBlock);
        if (currentRow < 0) {
            currentRow = 0;
        } else if (++currentRow >= rowCount) {
            resetToBlockLevel();
        }
        break;
    }

    case LEVEL_CHARACTER: {
        int charCount = getRowCharCount(currentBlock, currentRow);
        if (currentCharIndex < 0) {
            currentCharIndex = 0;
        } else if (++currentCharIndex >= charCount) {
            resetToBlockLevel();
        }
        break;
    }
    }
}

void SingleButtonSpecialCharacter::confirmSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        if (currentBlock >= 0) {
            currentLevel = LEVEL_ROW;
            currentRow = -1;
            currentCharIndex = -1;
        }
        break;

    case LEVEL_ROW:
        if (currentRow >= 0) {
            currentLevel = LEVEL_CHARACTER;
            currentCharIndex = -1;
        }
        break;

    case LEVEL_CHARACTER:
        if (currentCharIndex >= 0) {
            addCharacterToInput(getCharAt(currentBlock, currentRow, currentCharIndex));
            resetToBlockLevel();
        }
        break;
    }
}

void SingleButtonSpecialCharacter::resetToBlockLevel()
{
    currentLevel = LEVEL_BLOCK;
    currentBlock = -1;
    currentRow = -1;
    currentCharIndex = -1;
    lastPressTime = 0;
}

int SingleButtonSpecialCharacter::getBlockRowCount(int blockIndex) const
{
    return 3;
}

int SingleButtonSpecialCharacter::getRowCharCount(int blockIndex, int rowIndex) const
{
    return strlen(BLOCK_CHARS[blockIndex][rowIndex]);
}

char SingleButtonSpecialCharacter::getCharAt(int blockIndex, int rowIndex, int charIndex) const
{
    const char *row = BLOCK_CHARS[blockIndex][rowIndex];
    return (charIndex >= 0 && charIndex < (int)strlen(row)) ? row[charIndex] : 0;
}

void SingleButtonSpecialCharacter::addCharacterToInput(char c)
{
    inputText += c;
    if (c == '.' || c == '!' || c == '?') {
        shift = true;
    }
}

void SingleButtonSpecialCharacter::handleMenuSelection(int selection)
{
    SingleButtonInputBase::handleMenuSelection(selection);
}

void SingleButtonSpecialCharacter::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    drawGridInterface(display, x, y);
}

void SingleButtonSpecialCharacter::drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    int lineHeight = 10;
    int currentY = y;

    display->drawString(x, currentY, headerText.c_str());
    currentY += lineHeight + 2;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 2;

    std::string displayInput = getDisplayTextWithCursor();
    displayInput = formatDisplayTextWithScrolling(display, displayInput);
    display->drawString(x, currentY, displayInput.c_str());

    currentY += lineHeight + 3;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 3;

    int blockWidth = 30;
    int blockHeight = 24;
    int blockSpacing = 2;
    int startX = x + 2;

    for (int block = 0; block < 4; ++block) {
        int blockX = startX + block * (blockWidth + blockSpacing);
        bool isActiveBlock = (currentLevel == LEVEL_BLOCK && currentBlock == block);

        if (currentLevel == LEVEL_BLOCK) {
            drawBlock(display, block, blockX, currentY, blockWidth, blockHeight, isActiveBlock);
        } else if (currentBlock == block) {
            drawBlock(display, block, blockX, currentY, blockWidth, blockHeight, false);
        }
    }
}

void SingleButtonSpecialCharacter::drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height,
                                             bool highlighted)
{
    if (highlighted) {
        display->fillRect(x - 1, y - 1, width + 2, height + 6);
        display->setColor(BLACK);
    }

    int colWidth = width / 3;
    int rowHeight = height / 3;

    for (int row = 0; row < 3; ++row) {
        const char *rowStr = BLOCK_CHARS[blockIndex][row];
        int rowY = y + row * rowHeight;

        if (currentLevel == LEVEL_ROW && currentBlock == blockIndex && currentRow == row) {
            display->fillRect(x, rowY, width, rowHeight + 6);
            if (!highlighted) {
                display->setColor(BLACK);
            }
        }

        bool showRow = (currentLevel == LEVEL_BLOCK) ||
                       (currentLevel == LEVEL_ROW && currentBlock == blockIndex) ||
                       (currentLevel == LEVEL_CHARACTER && currentBlock == blockIndex && currentRow == row);

        if (showRow) {
            for (int col = 0; col < (int)strlen(rowStr) && col < 3; ++col) {
                int colX = x + col * colWidth;
                bool isCharHighlighted = (currentLevel == LEVEL_CHARACTER && currentBlock == blockIndex &&
                                          currentRow == row && currentCharIndex == col);

                if (isCharHighlighted) {
                    display->fillRect(colX, rowY, colWidth, rowHeight + 6);
                    if (!highlighted) {
                        display->setColor(BLACK);
                    }
                }

                char str[2] = {rowStr[col], '\0'};
                int textX = colX + colWidth / 2 - 3;
                int textY = rowY + (rowHeight - 8) / 2;
                display->drawString(textX, textY, str);

                if (isCharHighlighted && !highlighted) {
                    display->setColor(WHITE);
                }
            }
        }

        if (currentLevel == LEVEL_ROW && currentBlock == blockIndex && currentRow == row && !highlighted) {
            display->setColor(WHITE);
        }
    }

    if (highlighted) {
        display->setColor(WHITE);
    }
}

void SingleButtonSpecialCharacter::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!active) {
        return;
    }

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }

    drawInterface(display, x, y);
}

} // namespace graphics

#endif