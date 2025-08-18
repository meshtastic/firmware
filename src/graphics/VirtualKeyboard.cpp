#include "VirtualKeyboard.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "main.h"
#include <Arduino.h>

namespace graphics
{

VirtualKeyboard::VirtualKeyboard() : cursorRow(0), cursorCol(0), lastActivityTime(millis())
{
    initializeKeyboard();
    // Set cursor to H(2, 5)
    cursorRow = 2;
    cursorCol = 5;
}

VirtualKeyboard::~VirtualKeyboard() {}

void VirtualKeyboard::initializeKeyboard()
{
    // New 4-row layout with 10 characters + 1 action key per row (11 columns):
    // 1) 1 2 3 4 5 6 7 8 9 0 BACK
    // 2) q w e r t y u i o p ENTER
    // 3) a s d f g h j k l ; SPACE
    // 4) z x c v b n m . , ? ESC
    static const char LAYOUT[KEYBOARD_ROWS][KEYBOARD_COLS] = {{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\b'},
                                                              {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '\n'},
                                                              {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', ' '},
                                                              {'z', 'x', 'c', 'v', 'b', 'n', 'm', '.', ',', '?', '\x1b'}};

    // Derive layout dimensions and assert they match the configured keyboard grid
    constexpr int LAYOUT_ROWS = (int)(sizeof(LAYOUT) / sizeof(LAYOUT[0]));
    constexpr int LAYOUT_COLS = (int)(sizeof(LAYOUT[0]) / sizeof(LAYOUT[0][0]));
    static_assert(LAYOUT_ROWS == KEYBOARD_ROWS, "LAYOUT rows must equal KEYBOARD_ROWS");
    static_assert(LAYOUT_COLS == KEYBOARD_COLS, "LAYOUT cols must equal KEYBOARD_COLS");

    // Initialize all keys to empty first
    for (int row = 0; row < LAYOUT_ROWS; row++) {
        for (int col = 0; col < LAYOUT_COLS; col++) {
            keyboard[row][col] = {0, VK_CHAR, 0, 0, 0, 0};
        }
    }

    // Fill keyboard from the 2D layout
    for (int row = 0; row < LAYOUT_ROWS; row++) {
        for (int col = 0; col < LAYOUT_COLS; col++) {
            char ch = LAYOUT[row][col];
            // No empty slots in the simplified layout

            VirtualKeyType type = VK_CHAR;
            if (ch == '\b') {
                type = VK_BACKSPACE;
            } else if (ch == '\n') {
                type = VK_ENTER;
            } else if (ch == '\x1b') { // ESC
                type = VK_ESC;
            } else if (ch == ' ') {
                type = VK_SPACE;
            }

            // Make action keys wider to fit text while keeping the last column aligned
            uint8_t width = (type == VK_BACKSPACE || type == VK_ENTER || type == VK_SPACE) ? (KEY_WIDTH * 3) : KEY_WIDTH;
            keyboard[row][col] = {ch, type, (uint8_t)(col * KEY_WIDTH), (uint8_t)(row * KEY_HEIGHT), width, KEY_HEIGHT};
        }
    }
}

void VirtualKeyboard::draw(OLEDDisplay *display, int16_t offsetX, int16_t offsetY)
{
    // Base styles
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Screen geometry
    const int screenW = display->getWidth();
    const int screenH = display->getHeight();

    // Decide wide-screen mode: if there is comfortable width, allow taller keys and reserve fixed width for last column labels
    // Heuristic: if screen width >= 200px (e.g., 240x135), treat as wide
    const bool isWide = screenW >= 200;

    // Determine last-column label max width
    display->setFont(FONT_SMALL);
    const int wENTER = display->getStringWidth("ENTER");
    int lastColLabelW = wENTER; // ENTER is usually the widest
    // Smaller padding on very small screens to avoid excessive whitespace
    const int lastColPad = (screenW <= 128 ? 2 : 6);
    const int reservedLastColW = lastColLabelW + lastColPad; // reserved width for last column keys

    // Dynamic key geometry
    int cellH = KEY_HEIGHT;
    if (isWide) {
        cellH = KEY_HEIGHT + 3; // slightly taller on wide screens
    }

    // Always reserve width for the rightmost text column to avoid overlap on small screens
    int cellW = 0;
    int leftoverW = 0;
    {
        const int leftCols = KEYBOARD_COLS - 1; // 10 input characters
        int usableW = screenW - reservedLastColW;
        if (usableW < leftCols) {
            // Guard: ensure at least 1px per left cell if labels are extremely wide (unlikely)
            usableW = leftCols;
        }
        cellW = usableW / leftCols;
        leftoverW = usableW - cellW * leftCols; // distribute extra pixels over left columns (left to right)
    }

    // Keyboard placement from bottom
    const int keyboardHeight = KEYBOARD_ROWS * cellH;
    int keyboardStartY = screenH - keyboardHeight;
    if (keyboardStartY < 0)
        keyboardStartY = 0;

    // Draw input area above keyboard
    drawInputArea(display, offsetX, offsetY, keyboardStartY);

    // Precompute per-column x and width with leftover distributed over left columns for even spacing
    int colX[KEYBOARD_COLS];
    int colW[KEYBOARD_COLS];
    int runningX = offsetX;
    for (int col = 0; col < KEYBOARD_COLS - 1; ++col) {
        int wcol = cellW + (col < leftoverW ? 1 : 0);
        colX[col] = runningX;
        colW[col] = wcol;
        runningX += wcol;
    }
    // Last column
    colX[KEYBOARD_COLS - 1] = runningX;
    colW[KEYBOARD_COLS - 1] = reservedLastColW;

    // Draw keyboard grid
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            const VirtualKey &k = keyboard[row][col];
            if (k.character != 0 || k.type != VK_CHAR) {
                const bool isLastCol = (col == KEYBOARD_COLS - 1);
                int x = colX[col];
                int w = colW[col];
                int y = offsetY + keyboardStartY + row * cellH;
                int h = cellH;
                bool selected = (row == cursorRow && col == cursorCol);
                drawKey(display, k, selected, x, y, (uint8_t)w, (uint8_t)h, isLastCol);
            }
        }
    }
}

void VirtualKeyboard::drawInputArea(OLEDDisplay *display, int16_t offsetX, int16_t offsetY, int16_t keyboardStartY)
{
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    const int screenWidth = display->getWidth();
    const int lineH = FONT_HEIGHT_SMALL;

    int headerHeight = 0;
    if (!headerText.empty()) {
        display->drawString(offsetX + 2, offsetY, headerText.c_str());
        headerHeight = 10;
    }

    // Input box - from below header down to just above the keyboard
    const int boxX = offsetX + 2;
    const int boxY = offsetY + headerHeight + 2;
    const int boxWidth = screenWidth - 4;
    int availableH = keyboardStartY - boxY - 2; // small gap above keyboard
    if (availableH < lineH + 2)
        availableH = lineH + 2; // ensure minimum
    const int boxHeight = availableH;

    // Draw box border
    display->drawRect(boxX, boxY, boxWidth, boxHeight);

    // Text rendering: multi-line if space allows (>= 2 lines), else single-line with leading ellipsis
    const int textX = boxX + 2;
    const int maxTextWidth = boxWidth - 4;
    const int maxLines = (boxHeight - 2) / lineH;
    if (maxLines >= 2) {
        std::string remaining = inputText;
        int lineY = boxY + 1;
        for (int line = 0; line < maxLines && !remaining.empty(); ++line) {
            int bestLen = 0;
            for (int len = 1; len <= (int)remaining.size(); ++len) {
                int w = display->getStringWidth(remaining.substr(0, len).c_str());
                if (w <= maxTextWidth)
                    bestLen = len;
                else
                    break;
            }
            if (bestLen == 0)
                break;
            std::string chunk = remaining.substr(0, bestLen);
            display->drawString(textX, lineY, chunk.c_str());
            remaining.erase(0, bestLen);
            lineY += lineH;
        }
        // Optional: draw cursor at end of last line could be added if needed
    } else {
        std::string displayText = inputText;
        int textW = display->getStringWidth(displayText.c_str());
        std::string scrolled = displayText;
        if (textW > maxTextWidth) {
            // Trim from the left until it fits
            while (textW > maxTextWidth && !scrolled.empty()) {
                scrolled.erase(0, 1);
                textW = display->getStringWidth(scrolled.c_str());
            }
            // Add leading ellipsis and ensure it still fits
            if (scrolled != displayText) {
                scrolled = "..." + scrolled;
                textW = display->getStringWidth(scrolled.c_str());
                // If adding ellipsis causes overflow, trim more after the ellipsis
                while (textW > maxTextWidth && scrolled.size() > 3) {
                    scrolled.erase(3, 1); // remove chars after the ellipsis
                    textW = display->getStringWidth(scrolled.c_str());
                }
            }
        } else {
            // Keep textW in sync with what we draw
            textW = display->getStringWidth(scrolled.c_str());
        }

        const int innerLeft = boxX + 1;
        const int innerRight = boxX + boxWidth - 2;
        const int innerTop = boxY + 1;
        const int innerBottom = boxY + boxHeight - 2;

        const int textY = boxY + 1;
        if (!scrolled.empty()) {
            display->drawString(textX, textY, scrolled.c_str());
        }

        // Cursor at end of visible text: keep within inner box and within text height
        int cursorX = textX + textW;
        if (cursorX > innerRight)
            cursorX = innerRight;

        // Apply vertical padding so caret doesn't touch top/bottom
        int caretPadY = 2;
        if (boxHeight >= lineH + 4)
            caretPadY = 3; // if box is taller than minimal, allow larger gap
        int cursorTop = textY + caretPadY;
        int cursorH = lineH - caretPadY * 2;
        if (cursorH < 1)
            cursorH = 1;
        // Clamp vertical bounds to stay inside the inner rect
        if (cursorTop < innerTop)
            cursorTop = innerTop;
        if (cursorTop + cursorH - 1 > innerBottom)
            cursorH = innerBottom - cursorTop + 1;
        if (cursorH < 1)
            cursorH = 1;

        // Only draw if cursor is inside inner bounds
        if (cursorX >= innerLeft && cursorX <= innerRight) {
            display->drawVerticalLine(cursorX, cursorTop, cursorH);
        }
    }
}

void VirtualKeyboard::drawKey(OLEDDisplay *display, const VirtualKey &key, bool selected, int16_t x, int16_t y, uint8_t width,
                              uint8_t height, bool isLastCol)
{
    // Draw key content
    display->setFont(FONT_SMALL);
    const int fontH = FONT_HEIGHT_SMALL; // actual pixel height of current font
    // Build label and metrics first
    std::string keyText;
    if (key.type == VK_BACKSPACE || key.type == VK_ENTER || key.type == VK_SPACE || key.type == VK_ESC) {
        // Keep literal text labels for the action keys on the rightmost column
        keyText = (key.type == VK_BACKSPACE) ? "BACK"
                  : (key.type == VK_ENTER)   ? "ENTER"
                  : (key.type == VK_SPACE)   ? "SPACE"
                  : (key.type == VK_ESC)     ? "ESC"
                                             : "";
    } else {
        char c = getCharForKey(key, false);
        keyText = (key.character == ' ' || key.character == '_') ? "_" : std::string(1, c);
    }

    int textWidth = display->getStringWidth(keyText.c_str());
    // Label alignment
    // - Rightmost action column: right-align text with a small right padding (~2px) so it hugs screen edge neatly.
    // - Other keys: center horizontally; use ceil-style rounding to avoid appearing left-biased on odd widths.
    int textX;
    if (isLastCol) {
        const int rightPad = 2;
        textX = x + width - textWidth - rightPad;
        if (textX < x)
            textX = x; // guard
    } else {
        textX = x + ((width - textWidth) + 1) / 2; // ceil((w - tw)/2)
    }
    int textY = y + (height - fontH) / 2; // baseline for text
    // Per-character vertical nudge for better visual centering (only for single-character keys)
    if (key.type == VK_CHAR) {
        int nudge = 0;
        if (keyText == "j") {
            nudge = 1; // j up 1px
        } else if (keyText.find_first_of("gpqy") != std::string::npos) {
            nudge = 2; // g/p/q/y up 2px
        } else if (keyText == ";" || keyText == "." || keyText == ",") {
            nudge = 1; // ; . , up 1px to appear vertically centered
        }
        if (nudge) {
            textY -= nudge;
            if (textY < 0)
                textY = 0;
        }
    }

    // Selected: for action text buttons, highlight fits text width; for char keys, fill entire key
    if (selected) {
        display->setColor(WHITE);
        bool isAction = (key.type == VK_BACKSPACE || key.type == VK_ENTER || key.type == VK_SPACE || key.type == VK_ESC);
        if (isAction) {
            const int padX = 2; // small horizontal padding around text
            int hlX = textX - padX;
            int hlW = textWidth + padX * 2;
            // Constrain highlight within the key's horizontal span
            int keyRight = x + width;
            if (hlX < x) {
                hlW -= (x - hlX);
                hlX = x;
            }
            int maxW = keyRight - hlX;
            if (hlW > maxW)
                hlW = maxW;
            if (hlW < 1)
                hlW = 1;
            display->fillRect(hlX, y, hlW, height);
        } else {
            display->fillRect(x, y, width, height);
        }
        display->setColor(BLACK);
    } else {
        display->setColor(WHITE);
    }

    display->drawString(textX, textY, keyText.c_str());
}

char VirtualKeyboard::getCharForKey(const VirtualKey &key, bool isLongPress)
{
    if (key.type != VK_CHAR) {
        return key.character;
    }

    char c = key.character;

    // Long-press: only keep letter lowercase->uppercase conversion; remove other symbol mappings
    if (isLongPress && c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    return c;
}

void VirtualKeyboard::moveCursorDelta(int dRow, int dCol)
{
    resetTimeout();
    // wrap around rows and cols in the 4x11 grid
    int r = (int)cursorRow + dRow;
    int c = (int)cursorCol + dCol;
    if (r < 0)
        r = KEYBOARD_ROWS - 1;
    else if (r >= KEYBOARD_ROWS)
        r = 0;
    if (c < 0)
        c = KEYBOARD_COLS - 1;
    else if (c >= KEYBOARD_COLS)
        c = 0;
    cursorRow = (uint8_t)r;
    cursorCol = (uint8_t)c;
}

void VirtualKeyboard::moveCursorUp()
{
    moveCursorDelta(-1, 0);
}
void VirtualKeyboard::moveCursorDown()
{
    moveCursorDelta(1, 0);
}
void VirtualKeyboard::moveCursorLeft()
{
    moveCursorDelta(0, -1);
}
void VirtualKeyboard::moveCursorRight()
{
    moveCursorDelta(0, 1);
}

void VirtualKeyboard::handlePress()
{
    resetTimeout(); // Reset timeout on any input activity

    const VirtualKey &key = keyboard[cursorRow][cursorCol];

    // Don't handle press if the key is empty (but allow special keys)
    if (key.character == 0 && key.type == VK_CHAR) {
        return;
    }

    // For character keys, insert lowercase character
    if (key.type == VK_CHAR) {
        insertCharacter(getCharForKey(key, false)); // false = lowercase/normal char
        return;
    }

    // Handle non-character keys immediately
    switch (key.type) {
    case VK_BACKSPACE:
        deleteCharacter();
        break;
    case VK_ENTER:
        submitText();
        break;
    case VK_SPACE:
        insertCharacter(' ');
        break;
    case VK_ESC:
        if (onTextEntered) {
            std::function<void(const std::string &)> callback = onTextEntered;
            onTextEntered = nullptr;
            inputText = "";
            callback("");
        }
        return;
    default:
        break;
    }
}

void VirtualKeyboard::handleLongPress()
{
    resetTimeout(); // Reset timeout on any input activity

    const VirtualKey &key = keyboard[cursorRow][cursorCol];

    // Don't handle press if the key is empty (but allow special keys)
    if (key.character == 0 && key.type == VK_CHAR) {
        return;
    }

    // For character keys, insert uppercase/alternate character
    if (key.type == VK_CHAR) {
        insertCharacter(getCharForKey(key, true)); // true = uppercase/alternate char
        return;
    }

    // For non-character keys, long press behaves the same as regular press
    switch (key.type) {
    case VK_BACKSPACE:
        deleteCharacter();
        break;
    case VK_ENTER:
        submitText();
        break;
    case VK_SPACE:
        insertCharacter(' ');
        break;
    case VK_ESC:
        if (onTextEntered) {
            onTextEntered("");
        }
        break;
    default:
        break;
    }
}

void VirtualKeyboard::insertCharacter(char c)
{
    if (inputText.length() < 160) { // Reasonable text length limit
        inputText += c;
    }
}

void VirtualKeyboard::deleteCharacter()
{
    if (!inputText.empty()) {
        inputText.pop_back();
    }
}

void VirtualKeyboard::submitText()
{
    LOG_INFO("Virtual keyboard: submitting text '%s'", inputText.c_str());

    // Only submit if text is not empty
    if (!inputText.empty() && onTextEntered) {
        // Store callback and text to submit before clearing callback
        std::function<void(const std::string &)> callback = onTextEntered;
        std::string textToSubmit = inputText;
        onTextEntered = nullptr;
        // Don't clear inputText here - let the calling module handle cleanup
        // inputText = "";  // Removed: keep text visible until module cleans up
        callback(textToSubmit);
    } else if (inputText.empty()) {
        // For empty text, just ignore the submission - don't clear callback
        // This keeps the virtual keyboard responsive for further input
        LOG_INFO("Virtual keyboard: empty text submitted, ignoring - keyboard remains active");
    } else {
        // No callback available
        if (screen) {
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        }
    }
}

void VirtualKeyboard::setInputText(const std::string &text)
{
    inputText = text;
}

std::string VirtualKeyboard::getInputText() const
{
    return inputText;
}

void VirtualKeyboard::setHeader(const std::string &header)
{
    headerText = header;
}

void VirtualKeyboard::setCallback(std::function<void(const std::string &)> callback)
{
    onTextEntered = callback;
}

void VirtualKeyboard::resetTimeout()
{
    lastActivityTime = millis();
}

bool VirtualKeyboard::isTimedOut() const
{
    return (millis() - lastActivityTime) > TIMEOUT_MS;
}

} // namespace graphics
