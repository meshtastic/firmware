#include "input/SingleButtonGridKeyboard.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

namespace graphics
{

const char *SingleButtonGridKeyboard::BLOCK_CHARS[4][3] = {
    {"ABC", "DEF", "GHI"},
    {"JKL", "MNO", "PQR"},
    {"STU", "VWX", "YZ?"},
    {" ,.", "(?!", ");:"}
};

SingleButtonGridKeyboard &SingleButtonGridKeyboard::instance()
{
    static SingleButtonGridKeyboard inst;
    return inst;
}

SingleButtonGridKeyboard::SingleButtonGridKeyboard() : SingleButtonInputBase("SingleButtonGridKeyboard") {}

void SingleButtonGridKeyboard::start(const char *header, const char *initialText, uint32_t durationMs,
                                     std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header, initialText, durationMs, cb);
    resetToBlockLevel();
}

void SingleButtonGridKeyboard::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
    lastPressTime = now;
}

void SingleButtonGridKeyboard::handleButtonRelease(uint32_t now, uint32_t duration)
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

void SingleButtonGridKeyboard::handleIdle(uint32_t now)
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

void SingleButtonGridKeyboard::advanceSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        if (currentBlock < 0) {
            currentBlock = 0;
        } else if (++currentBlock >= 4) {
            currentBlock = 0;
        }
        break;

    case LEVEL_COLUMN: {
        int colCount = getBlockColumnCount(currentBlock);
        if (currentColumn < 0) {
            currentColumn = 0;
        } else if (++currentColumn >= colCount) {
            resetToBlockLevel();
        }
        break;
    }

    case LEVEL_CHARACTER: {
        int charCount = getColumnCharCount(currentBlock, currentColumn);
        if (currentCharIndex < 0) {
            currentCharIndex = 0;
        } else if (++currentCharIndex >= charCount) {
            resetToBlockLevel();
        }
        break;
    }
    }
}

void SingleButtonGridKeyboard::confirmSelection()
{
    switch (currentLevel) {
    case LEVEL_BLOCK:
        if (currentBlock >= 0) {
            currentLevel = LEVEL_COLUMN;
            currentColumn = -1;
            currentCharIndex = -1;
        }
        break;

    case LEVEL_COLUMN:
        if (currentColumn >= 0) {
            currentLevel = LEVEL_CHARACTER;
            currentCharIndex = -1;
        }
        break;

    case LEVEL_CHARACTER:
        if (currentCharIndex >= 0) {
            addCharacterToInput(getCharAt(currentBlock, currentColumn, currentCharIndex));
            resetToBlockLevel();
        }
        break;
    }
}

void SingleButtonGridKeyboard::resetToBlockLevel()
{
    currentLevel = LEVEL_BLOCK;
    currentBlock = -1;
    currentColumn = -1;
    currentCharIndex = -1;
    lastPressTime = 0;
}

int SingleButtonGridKeyboard::getBlockColumnCount(int blockIndex) const
{
    return 3;
}

int SingleButtonGridKeyboard::getColumnCharCount(int blockIndex, int columnIndex) const
{
    return strlen(BLOCK_CHARS[blockIndex][columnIndex]);
}

char SingleButtonGridKeyboard::getCharAt(int blockIndex, int columnIndex, int charIndex) const
{
    const char *col = BLOCK_CHARS[blockIndex][columnIndex];
    return (charIndex >= 0 && charIndex < (int)strlen(col)) ? col[charIndex] : 0;
}

void SingleButtonGridKeyboard::addCharacterToInput(char c)
{
    if (shift) {
        c = toupper(c);
        if (autoShift) {
            shift = false;
        }
    } else {
        c = tolower(c);
    }

    inputText += c;

    if (c == '.' || c == '!' || c == '?') {
        shift = true;
    }
}

void SingleButtonGridKeyboard::handleMenuSelection(int selection)
{
    SingleButtonInputBase::handleMenuSelection(selection);
}

void SingleButtonGridKeyboard::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    drawGridInterface(display, x, y);
}

void SingleButtonGridKeyboard::drawGridInterface(OLEDDisplay *display, int16_t x, int16_t y)
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

void SingleButtonGridKeyboard::drawBlock(OLEDDisplay *display, int blockIndex, int x, int y, int width, int height,
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

        if (currentLevel == LEVEL_COLUMN && currentBlock == blockIndex && currentColumn == row) {
            display->fillRect(x, rowY, width, rowHeight + 6);
            if (!highlighted) {
                display->setColor(BLACK);
            }
        }

        bool showRow = (currentLevel == LEVEL_BLOCK) ||
                       (currentLevel == LEVEL_COLUMN && currentBlock == blockIndex) ||
                       (currentLevel == LEVEL_CHARACTER && currentBlock == blockIndex && currentColumn == row);

        if (showRow) {
            for (int col = 0; col < (int)strlen(rowStr) && col < 3; ++col) {
                char c = rowStr[col];
                if (shift) {
                    c = toupper(c);
                } else if (isalpha(c)) {
                    c = tolower(c);
                }

                int colX = x + col * colWidth;
                bool isCharHighlighted = (currentLevel == LEVEL_CHARACTER && currentBlock == blockIndex &&
                                          currentColumn == row && currentCharIndex == col);

                if (isCharHighlighted) {
                    display->fillRect(colX, rowY, colWidth, rowHeight + 6);
                    if (!highlighted) {
                        display->setColor(BLACK);
                    }
                }

                char str[2] = {c, '\0'};
                int textX = colX + colWidth / 2 - 3;
                int textY = rowY + (rowHeight - 8) / 2;
                display->drawString(textX, textY, str);

                if (isCharHighlighted && !highlighted) {
                    display->setColor(WHITE);
                }
            }
        }

        if (currentLevel == LEVEL_COLUMN && currentBlock == blockIndex && currentColumn == row && !highlighted) {
            display->setColor(WHITE);
        }
    }

    if (highlighted) {
        display->setColor(WHITE);
    }
}

void SingleButtonGridKeyboard::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
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