#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./KeyboardApplet.h"

using namespace NicheGraphics;

InkHUD::KeyboardApplet::KeyboardApplet()
{
    // Calculate row widths
    for (uint8_t row = 0; row < KBD_ROWS; row++) {
        rowWidths[row] = 0;
        for (uint8_t col = 0; col < KBD_COLS; col++)
            rowWidths[row] += keyWidths[row * KBD_COLS + col];
    }
}

void InkHUD::KeyboardApplet::onRender(bool full)
{
    uint16_t em = fontSmall.lineHeight(); // 16 pt
    uint16_t keyH = Y(1.0) / KBD_ROWS;
    int16_t keyTopPadding = (keyH - fontSmall.lineHeight()) / 2;

    if (full) { // Draw full keyboard
        for (uint8_t row = 0; row < KBD_ROWS; row++) {

            // Calculate the remaining space to be used as padding
            int16_t keyXPadding = X(1.0) - ((rowWidths[row] * em) >> 4);

            // Draw keys
            uint16_t xPos = 0;
            for (uint8_t col = 0; col < KBD_COLS; col++) {
                Color fgcolor = BLACK;
                uint8_t index = row * KBD_COLS + col;
                uint16_t keyX = ((xPos * em) >> 4) + ((col * keyXPadding) / (KBD_COLS - 1));
                uint16_t keyY = row * keyH;
                uint16_t keyW = (keyWidths[index] * em) >> 4;
                if (index == selectedKey) {
                    fgcolor = WHITE;
                    fillRect(keyX, keyY, keyW, keyH, BLACK);
                }
                drawKeyLabel(keyX, keyY + keyTopPadding, keyW, keys[index], fgcolor);
                xPos += keyWidths[index];
            }
        }
    } else { // Only draw the difference
        if (selectedKey != prevSelectedKey) {
            // Draw previously selected key
            uint8_t row = prevSelectedKey / KBD_COLS;
            int16_t keyXPadding = X(1.0) - ((rowWidths[row] * em) >> 4);
            uint16_t xPos = 0;
            for (uint8_t i = prevSelectedKey - (prevSelectedKey % KBD_COLS); i < prevSelectedKey; i++)
                xPos += keyWidths[i];
            uint16_t keyX = ((xPos * em) >> 4) + (((prevSelectedKey % KBD_COLS) * keyXPadding) / (KBD_COLS - 1));
            uint16_t keyY = row * keyH;
            uint16_t keyW = (keyWidths[prevSelectedKey] * em) >> 4;
            fillRect(keyX, keyY, keyW, keyH, WHITE);
            drawKeyLabel(keyX, keyY + keyTopPadding, keyW, keys[prevSelectedKey], BLACK);

            // Draw newly selected key
            row = selectedKey / KBD_COLS;
            keyXPadding = X(1.0) - ((rowWidths[row] * em) >> 4);
            xPos = 0;
            for (uint8_t i = selectedKey - (selectedKey % KBD_COLS); i < selectedKey; i++)
                xPos += keyWidths[i];
            keyX = ((xPos * em) >> 4) + (((selectedKey % KBD_COLS) * keyXPadding) / (KBD_COLS - 1));
            keyY = row * keyH;
            keyW = (keyWidths[selectedKey] * em) >> 4;
            fillRect(keyX, keyY, keyW, keyH, BLACK);
            drawKeyLabel(keyX, keyY + keyTopPadding, keyW, keys[selectedKey], WHITE);
        }
    }

    prevSelectedKey = selectedKey;
}

// Draw the key label corresponding to the char
// for most keys it draws the character itself
// for ['\b', '\n', ' ', '\x1b'] it draws special glyphs
void InkHUD::KeyboardApplet::drawKeyLabel(uint16_t left, uint16_t top, uint16_t width, char key, Color color)
{
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
        uint16_t leftPadding = (width - 13) >> 1;
        drawBitmap(left + leftPadding, top + 1, bsBitmap, 13, 9, color);
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
        uint16_t leftPadding = (width - 12) >> 1;
        drawBitmap(left + leftPadding, top + 1, doneBitmap, 12, 9, color);
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
        uint16_t leftPadding = (width - 13) >> 1;
        drawBitmap(left + leftPadding, top + 1, spaceBitmap, 13, 9, color);
    } else if (key == '\x1b') {
        setTextColor(color);
        std::string keyText = "ESC";
        uint16_t leftPadding = (width - getTextWidth(keyText)) >> 1;
        printAt(left + leftPadding, top, keyText);
    } else {
        setTextColor(color);
        if (key >= 0x61)
            key -= 32; // capitalize
        std::string keyText = std::string(1, key);
        uint16_t leftPadding = (width - getTextWidth(keyText)) >> 1;
        printAt(left + leftPadding, top, keyText);
    }
}

void InkHUD::KeyboardApplet::onForeground()
{
    handleInput = true; // Intercept the button input for our applet

    // Select the first key
    selectedKey = 0;
    prevSelectedKey = 0;
}

void InkHUD::KeyboardApplet::onBackground()
{
    handleInput = false;
}

void InkHUD::KeyboardApplet::onButtonShortPress()
{
    char key = keys[selectedKey];
    if (key == '\n') {
        inkhud->freeTextDone();
        inkhud->closeKeyboard();
    } else if (key == '\x1b') {
        inkhud->freeTextCancel();
        inkhud->closeKeyboard();
    } else {
        inkhud->freeText(key);
    }
}

void InkHUD::KeyboardApplet::onButtonLongPress()
{
    char key = keys[selectedKey];
    if (key == '\n') {
        inkhud->freeTextDone();
        inkhud->closeKeyboard();
    } else if (key == '\x1b') {
        inkhud->freeTextCancel();
        inkhud->closeKeyboard();
    } else {
        if (key >= 0x61)
            key -= 32; // capitalize
        inkhud->freeText(key);
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
    if (selectedKey < KBD_COLS) // wrap
        selectedKey += KBD_COLS * (KBD_ROWS - 1);
    else // move 1 row back
        selectedKey -= KBD_COLS;

    // Request rendering over the previously drawn render
    requestUpdate(EInk::UpdateTypes::FAST, false);
    // Force an update to bypass lockRequests
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::KeyboardApplet::onNavDown()
{
    selectedKey += KBD_COLS;
    selectedKey %= (KBD_COLS * KBD_ROWS);

    // Request rendering over the previously drawn render
    requestUpdate(EInk::UpdateTypes::FAST, false);
    // Force an update to bypass lockRequests
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::KeyboardApplet::onNavLeft()
{
    if (selectedKey % KBD_COLS == 0) // wrap
        selectedKey += KBD_COLS - 1;
    else // move 1 column back
        selectedKey--;

    // Request rendering over the previously drawn render
    requestUpdate(EInk::UpdateTypes::FAST, false);
    // Force an update to bypass lockRequests
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::KeyboardApplet::onNavRight()
{
    if (selectedKey % KBD_COLS == KBD_COLS - 1) // wrap
        selectedKey -= KBD_COLS - 1;
    else // move 1 column forward
        selectedKey++;

    // Request rendering over the previously drawn render
    requestUpdate(EInk::UpdateTypes::FAST, false);
    // Force an update to bypass lockRequests
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

uint16_t InkHUD::KeyboardApplet::getKeyboardHeight()
{
    const uint16_t keyH = fontSmall.lineHeight() * 1.2;
    return keyH * KBD_ROWS;
}
#endif
