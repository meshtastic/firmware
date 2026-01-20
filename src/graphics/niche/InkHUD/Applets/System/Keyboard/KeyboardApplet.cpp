#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./KeyboardApplet.h"

using namespace NicheGraphics;

void InkHUD::KeyboardApplet::onRender()
{
    // Draw the qwerty interface
    drawKeyboard(0, 0, X(1.0), Y(1.0), selectCol, selectRow);
}

// Draw a qwerty keyboard
// The key at the select index is drawn inverted with a black background
void InkHUD::KeyboardApplet::drawKeyboard(uint16_t left, uint16_t top, uint16_t width, uint16_t height, uint16_t selectCol,
                                          uint8_t selectRow)
{
    setFont(fontSmall);
    uint16_t em = fontSmall.lineHeight(); // 16 pt
    uint16_t keyH = height / KBD_ROWS;
    int16_t keyTopPadding = (keyH - fontSmall.lineHeight()) / 2;

    for (uint8_t row = 0; row < KBD_ROWS; row++) {
        uint8_t col;
        // Calculate the remaining space to be used as padding
        uint16_t sum = 0;
        for (col = 0; col < KBD_COLS; col++)
            sum += keyWidths[row * KBD_COLS + col];
        int16_t keyXPadding = width - ((sum * em) >> 4);
        // Draw keys
        uint16_t xPos = 0;
        for (col = 0; col < KBD_COLS; col++) {
            Color fgcolor = BLACK;
            uint8_t index = row * KBD_COLS + col;
            uint16_t keyX = left + ((xPos * em) >> 4) + ((col * keyXPadding) / (KBD_COLS - 1));
            uint16_t keyY = top + row * keyH;
            uint16_t keyW = (keyWidths[index] * em) >> 4;
            if (col == selectCol && row == selectRow) {
                fgcolor = WHITE;
                fillRect(keyX, keyY, keyW, keyH, BLACK);
            }
            char key = keys[index];
            if (key == '\b') {
                // Draw backspace glyph: 13 x 9 px
                /**
                 *         [][][][][][][][][]
                 *       [][]              []
                 *     [][]    []      []  []
                 *   [][]        []  []    []
                 * [][]            []      []
                 *   [][]        []  []    []
                 *     [][]    []      []  []
                 *       [][]              []
                 *         [][][][][][][][][]
                 */
                const uint8_t bsBitmap[] = {0x0f, 0xf8, 0x18, 0x08, 0x32, 0x28, 0x61, 0x48, 0xc0,
                                            0x88, 0x61, 0x48, 0x32, 0x28, 0x18, 0x08, 0x0f, 0xf8};
                uint16_t keyLeftPadding = (keyW - 13) >> 1;
                drawBitmap(keyX + keyLeftPadding, keyY + keyTopPadding + 1, bsBitmap, 13, 9, fgcolor);
            } else if (key == '\n') {
                // Draw done glyph: 12 x 9 px
                /**
                 *                     [][]
                 *                   [][]
                 *                 [][]
                 *               [][]
                 *             [][]
                 * [][]      [][]
                 *   [][]  [][]
                 *     [][][]
                 *       []
                 */
                const uint8_t doneBitmap[] = {0x00, 0x30, 0x00, 0x60, 0x00, 0xc0, 0x01, 0x80, 0x03,
                                              0x00, 0xc6, 0x00, 0x6c, 0x00, 0x38, 0x00, 0x10, 0x00};
                uint16_t keyLeftPadding = (keyW - 12) >> 1;
                drawBitmap(keyX + keyLeftPadding, keyY + keyTopPadding + 1, doneBitmap, 12, 9, fgcolor);
            } else if (key == ' ') {
                // Draw space glyph: 13 x 9 px
                /**
                 *
                 *
                 *
                 *
                 * []                      []
                 * []                      []
                 * [][][][][][][][][][][][][]
                 *
                 *
                 */
                const uint8_t spaceBitmap[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
                                               0x08, 0x80, 0x08, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00};
                uint16_t keyLeftPadding = (keyW - 13) >> 1;
                drawBitmap(keyX + keyLeftPadding, keyY + keyTopPadding + 1, spaceBitmap, 13, 9, fgcolor);
            } else if (key == '\x1b') {
                setTextColor(fgcolor);
                std::string keyText = "ESC";
                uint16_t keyLeftPadding = (keyW - getTextWidth(keyText)) >> 1;
                printAt(keyX + keyLeftPadding, keyY + keyTopPadding, keyText);
            } else {
                setTextColor(fgcolor);
                if (key >= 0x61)
                    key -= 32; // capitalize
                std::string keyText = std::string(1, key);
                uint16_t keyLeftPadding = (keyW - getTextWidth(keyText)) >> 1;
                printAt(keyX + keyLeftPadding, keyY + keyTopPadding, keyText);
            }
            xPos += keyWidths[index];
        }
    }
}

void InkHUD::KeyboardApplet::onForeground()
{
    handleInput = true; // Intercept the button input for our applet

    // Select the first key
    selectCol = 0;
    selectRow = 0;
}

void InkHUD::KeyboardApplet::onBackground()
{
    handleInput = false;
}

void InkHUD::KeyboardApplet::onButtonShortPress()
{
    char ch = keys[selectRow * KBD_COLS + selectCol];
    if (ch == '\n') {
        inkhud->freeTextDone();
        inkhud->closeKeyboard();
    } else if (ch == '\x1b') {
        inkhud->freeTextCancel();
        inkhud->closeKeyboard();
    } else {
        inkhud->freeText(ch);
    }
}

void InkHUD::KeyboardApplet::onButtonLongPress()
{
    char ch = keys[selectRow * KBD_COLS + selectCol];
    if (ch == '\n') {
        inkhud->freeTextDone();
        inkhud->closeKeyboard();
    } else if (ch == '\x1b') {
        inkhud->freeTextCancel();
        inkhud->closeKeyboard();
    } else {
        if (ch >= 0x61)
            ch -= 32; // capitalize
        inkhud->freeText(ch);
    }
}

void InkHUD::KeyboardApplet::onExitShort()
{
    inkhud->freeTextCancel();
    inkhud->closeKeyboard();
}

void InkHUD::KeyboardApplet::onExitLong()
{
    inkhud->freeTextCancel();
    inkhud->closeKeyboard();
}

void InkHUD::KeyboardApplet::onNavUp()
{
    if (selectRow == 0)
        selectRow = KBD_ROWS - 1;
    else
        selectRow--;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::KeyboardApplet::onNavDown()
{
    selectRow = (selectRow + 1) % KBD_ROWS;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::KeyboardApplet::onNavLeft()
{
    if (selectCol == 0)
        selectCol = KBD_COLS - 1;
    else
        selectCol--;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::KeyboardApplet::onNavRight()
{
    selectCol = (selectCol + 1) % KBD_COLS;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

uint16_t InkHUD::KeyboardApplet::getKeyboardHeight()
{
    const uint16_t keyH = fontSmall.lineHeight() * 1.2;
    return keyH * KBD_ROWS;
}
#endif
