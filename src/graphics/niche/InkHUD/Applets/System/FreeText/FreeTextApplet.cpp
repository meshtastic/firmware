#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./FreeTextApplet.h"

using namespace NicheGraphics;

void InkHUD::FreeTextApplet::onRender()
{
    // Calculate the keyboard position
    uint16_t kbdH = KBD_ROWS * fontSmall.lineHeight() * 1.2;
    uint16_t kbdTop = Y(1.0) - kbdH;

    // Draw the text input box
    drawInputField(0, 0, width(), kbdTop - 1, inkhud->freetext);

    // Draw the keyboard
    drawKeyboard(0, kbdTop, width(), kbdH, selectCol, selectRow);
}

// Draw a text input box with a cursor
// The printed text wraps and scrolls as it overflows
void InkHUD::FreeTextApplet::drawInputField(uint16_t left, uint16_t top, uint16_t width, uint16_t height, std::string text)
{
    setFont(fontSmall);
    uint16_t wrapMaxH = 0;

    // Draw the text, input box, and cursor
    // Adjusting the box for screen height
    while (wrapMaxH < height - fontSmall.lineHeight()) {
        wrapMaxH += fontSmall.lineHeight();
    }

    // If the text is so long that it goes outside of the input box, the text is actually rendered off screen.
    uint32_t textHeight = getWrappedTextHeight(0, width - 5, text);
    if (!text.empty()) {
        uint16_t textPadding = X(1.0) > Y(1.0) ? wrapMaxH - textHeight : wrapMaxH - textHeight + 1;
        if (textHeight > wrapMaxH)
            printWrapped(2, textPadding, width - 5, text);
        else
            printWrapped(2, top + 2, width - 5, text);
    }

    uint16_t textCursorX = text.empty() ? 1 : getCursorX();
    uint16_t textCursorY = text.empty() ? 0 : getCursorY() - fontSmall.lineHeight() + 3;

    if (textCursorX + 1 > width - 5) {
        textCursorX = getCursorX() - width + 5;
        textCursorY += fontSmall.lineHeight();
    }

    fillRect(textCursorX + 1, textCursorY, 1, fontSmall.lineHeight(), BLACK);

    // A white rectangle clears the top part of the screen for any text that's printed beyond the input box
    fillRect(0, 0, X(1.0), top, WHITE);
    // printAt(0, 0, header);
    drawRect(0, top, width, wrapMaxH + 5, BLACK);
}

// Draw a qwerty keyboard
// The key at the select index is drawn inverted with a black background
void InkHUD::FreeTextApplet::drawKeyboard(uint16_t left, uint16_t top, uint16_t width, uint16_t height, uint16_t selectCol,
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

void InkHUD::FreeTextApplet::onForeground()
{
    // Prevent most other applets from requesting update, and skip their rendering entirely
    // Another system applet with a higher precedence can potentially ignore this
    SystemApplet::lockRendering = true;
    SystemApplet::lockRequests = true;

    handleInput = true; // Intercept the button input for our applet

    // Select the first key
    selectCol = 0;
    selectRow = 0;
}

void InkHUD::FreeTextApplet::onBackground()
{
    // Allow normal update behavior to resume
    SystemApplet::lockRendering = false;
    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    // Special free text event for returning to the originating applet
    inkhud->freeTextClosed();

    // Need to force an update, as a polite request wouldn't be honored, seeing how we are now in the background
    // Usually, onBackground is followed by another applet's onForeground (which requests update), but not in this case
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::FreeTextApplet::onButtonShortPress()
{
    char ch = keys[selectRow * KBD_COLS + selectCol];
    if (ch == '\b') {
        if (!inkhud->freetext.empty()) {
            inkhud->freetext.pop_back();
            requestUpdate(EInk::UpdateTypes::FAST);
        }
    } else if (ch == '\n') {
        sendToBackground();
    } else if (ch == '\x1b') {
        inkhud->freetext.erase();
        sendToBackground();
    } else {
        inkhud->freetext += ch;
        requestUpdate(EInk::UpdateTypes::FAST);
    }
}

void InkHUD::FreeTextApplet::onButtonLongPress()
{
    char ch = keys[selectRow * KBD_COLS + selectCol];
    if (ch == '\b') {
        if (!inkhud->freetext.empty()) {
            inkhud->freetext.pop_back();
            requestUpdate(EInk::UpdateTypes::FAST);
        }
    } else if (ch == '\n') {
        sendToBackground();
    } else if (ch == '\x1b') {
        inkhud->freetext.erase();
        sendToBackground();
    } else {
        if (ch >= 0x61)
            ch -= 32; // capitalize
        inkhud->freetext += ch;
        requestUpdate(EInk::UpdateTypes::FAST);
    }
}

void InkHUD::FreeTextApplet::onExitShort()
{
    inkhud->freetext.erase();
    sendToBackground();
}

void InkHUD::FreeTextApplet::onExitLong()
{
    inkhud->freetext.erase();
    sendToBackground();
}

void InkHUD::FreeTextApplet::onNavUp()
{
    if (selectRow == 0)
        selectRow = KBD_ROWS - 1;
    else
        selectRow--;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::FreeTextApplet::onNavDown()
{
    selectRow = (selectRow + 1) % KBD_ROWS;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::FreeTextApplet::onNavLeft()
{
    if (selectCol == 0)
        selectCol = KBD_COLS - 1;
    else
        selectCol--;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::FreeTextApplet::onNavRight()
{
    selectCol = (selectCol + 1) % KBD_COLS;
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}
#endif
