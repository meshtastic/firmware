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
    // Set cursor to Q(0, 0)
    cursorRow = 0;
    cursorCol = 0;
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
    // Set initial color and font
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Draw input area (header + input box)
    drawInputArea(display, offsetX, offsetY);

    // Draw keyboard with proper layout
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            if (keyboard[row][col].character != 0 || keyboard[row][col].type != VK_CHAR) { // Include special keys
                bool selected = (row == cursorRow && col == cursorCol);
                drawKey(display, keyboard[row][col], selected, offsetX, offsetY + KEYBOARD_START_Y);
            }
        }
    }

    // No close button any more
}

void VirtualKeyboard::drawInputArea(OLEDDisplay *display, int16_t offsetX, int16_t offsetY)
{
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    int screenWidth = display->getWidth();

    int headerHeight = 0;
    if (!headerText.empty()) {
        display->drawString(offsetX + 2, offsetY, headerText.c_str());
        headerHeight = 10;
    }

    // Draw input box - positioned just below header
    int boxWidth = screenWidth - 4;
    int boxY = offsetY + headerHeight + 2;
    int boxHeight = 14; // Increased by 2 pixels

    // Draw box border
    display->drawRect(offsetX + 2, boxY, boxWidth, boxHeight);

    // Prepare display text
    std::string displayText = inputText;
    if (displayText.empty()) {
        displayText = ""; // Don't show placeholder when empty
    }

    // Handle text overflow with scrolling
    int textPadding = 4;
    int maxWidth = boxWidth - textPadding;
    int textWidth = display->getStringWidth(displayText.c_str());

    std::string scrolledText = displayText;
    if (textWidth > maxWidth) {
        // Scroll text to show the end (cursor position)
        while (textWidth > maxWidth && !scrolledText.empty()) {
            scrolledText = scrolledText.substr(1);
            textWidth = display->getStringWidth(scrolledText.c_str());
        }
        if (scrolledText != displayText) {
            scrolledText = "..." + scrolledText;
        }
    }

    // Draw text inside the box - properly centered vertically in the input box
    int textX = offsetX + 4;
    int textY = boxY; // Moved down by 1 pixel

    if (!scrolledText.empty()) {
        display->drawString(textX, textY, scrolledText.c_str());
    }

    // Draw cursor at the end of visible text - aligned with text baseline
    if (!inputText.empty() || true) { // Always show cursor for visibility
        int cursorX = textX + display->getStringWidth(scrolledText.c_str());
        // Ensure cursor stays within box bounds
        if (cursorX < offsetX + boxWidth - 2) {
            // Align cursor properly with the text baseline and height - moved down by 2 pixels
            display->drawVerticalLine(cursorX, textY + 2, 10);
        }
    }
}

void VirtualKeyboard::drawKey(OLEDDisplay *display, const VirtualKey &key, bool selected, int16_t offsetX, int16_t offsetY)
{
    int x = offsetX + key.x;
    int y = offsetY + key.y;

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
                                             : "SHIFT";
    } else {
        char c = getCharForKey(key, false);
        keyText = (key.character == ' ' || key.character == '_') ? "_" : std::string(1, c);
    }

    int textWidth = display->getStringWidth(keyText.c_str());
    // Right-align text for the last column keys to screen edge (~2px margin), otherwise center
    int colIndex = key.x / KEY_WIDTH;
    bool isLastCol = (colIndex == (KEYBOARD_COLS - 1));
    const int screenRight = display->getWidth() - 2; // keep ~2px margin from the right screen edge
    int textX = isLastCol ? (screenRight - textWidth) : (x + (key.width - textWidth) / 2);
    int textY = y + (key.height - fontH) / 2; // baseline for text
    // Per-character vertical nudge for better visual centering (only for single-character keys)
    if (key.type == VK_CHAR) {
        int nudge = 0;
        if (keyText == "j") {
            nudge = 1; // j up 1px
        } else if (keyText.find_first_of("gpqy") != std::string::npos) {
            nudge = 2; // g/p/q/y up 2px
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
            int keyRight = isLastCol ? screenRight : (x + key.width);
            if (hlX < x) {
                hlW -= (x - hlX);
                hlX = x;
            }
            int maxW = keyRight - hlX;
            if (hlW > maxW)
                hlW = maxW;
            if (hlW < 1)
                hlW = 1;
            display->fillRect(hlX, y, hlW, key.height);
        } else {
            display->fillRect(x, y, key.width, key.height);
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

    if (isLongPress) {
        if (c == '.') {
            return ',';
        } else if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
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
