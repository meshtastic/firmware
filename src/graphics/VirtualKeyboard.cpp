#include "VirtualKeyboard.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "main.h"
#include <Arduino.h>

namespace graphics
{

VirtualKeyboard::VirtualKeyboard()
    : cursorRow(0), cursorCol(0), closeButtonX(0), closeButtonY(0), closeButtonWidth(0), closeButtonHeight(0),
      cursorOnCloseButton(false), lastActivityTime(millis())
{
    initializeKeyboard();
    // Set cursor to Q(0, 0)
    cursorRow = 0;
    cursorCol = 0;
}

VirtualKeyboard::~VirtualKeyboard() {}

void VirtualKeyboard::initializeKeyboard()
{
    // Initialize all keys to empty first
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            keyboard[row][col] = {0, VK_CHAR, 0, 0, 0, 0};
        }
    }

    // Row 0: q w e r t y u i o p 0 1 2 3
    const char *row0 = "qwertyuiop0123";
    for (int i = 0; i < 14; i++) {
        keyboard[0][i] = {row0[i], VK_CHAR, (uint8_t)(i * KEY_WIDTH), 0, KEY_WIDTH, KEY_HEIGHT};
    }

    // Row 1: a s d f g h j k l ← 4 5 6
    const char *row1 = "asdfghjkl";
    for (int i = 0; i < 9; i++) {
        keyboard[1][i] = {row1[i], VK_CHAR, (uint8_t)(i * KEY_WIDTH), KEY_HEIGHT, KEY_WIDTH, KEY_HEIGHT};
    }
    // Backspace key (2 chars wide)
    keyboard[1][9] = {'\b', VK_BACKSPACE, 9 * KEY_WIDTH, KEY_HEIGHT, KEY_WIDTH * 2, KEY_HEIGHT};
    // Numbers 4, 5, 6
    const char *numbers456 = "456";
    for (int i = 0; i < 3; i++) {
        keyboard[1][11 + i] = {numbers456[i], VK_CHAR, (uint8_t)((11 + i) * KEY_WIDTH), KEY_HEIGHT, KEY_WIDTH, KEY_HEIGHT};
    }

    // Row 2: z x c v b n m _ . OK 7 8 9
    const char *row2 = "zxcvbnm_.";
    for (int i = 0; i < 9; i++) {
        keyboard[2][i] = {row2[i], VK_CHAR, (uint8_t)(i * KEY_WIDTH), 2 * KEY_HEIGHT, KEY_WIDTH, KEY_HEIGHT};
    }
    // OK key (Enter) - 2 chars wide
    keyboard[2][9] = {'\n', VK_ENTER, 9 * KEY_WIDTH, 2 * KEY_HEIGHT, KEY_WIDTH * 2, KEY_HEIGHT};
    // Numbers 7, 8, 9
    const char *numbers789 = "789";
    for (int i = 0; i < 3; i++) {
        keyboard[2][11 + i] = {numbers789[i], VK_CHAR, (uint8_t)((11 + i) * KEY_WIDTH), 2 * KEY_HEIGHT, KEY_WIDTH, KEY_HEIGHT};
    }
}

void VirtualKeyboard::draw(OLEDDisplay *display, int16_t offsetX, int16_t offsetY)
{
    // Clear the display to avoid overlapping with other UI elements
    display->clear();

    // Set initial color and font
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Draw input area (header + input box)
    drawInputArea(display, offsetX, offsetY);

    // Draw keyboard with proper QWERTY layout
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            if (keyboard[row][col].character != 0 || keyboard[row][col].type != VK_CHAR) { // Include special keys
                bool selected = (row == cursorRow && col == cursorCol && !cursorOnCloseButton);
                drawKey(display, keyboard[row][col], selected, offsetX, offsetY + KEYBOARD_START_Y);
            }
        }
    }

    drawCloseButton(display, offsetX, offsetY, cursorOnCloseButton);
}

void VirtualKeyboard::drawCloseButton(OLEDDisplay *display, int16_t offsetX, int16_t offsetY, bool selected)
{
    if (closeButtonX == 0 && closeButtonY == 0) {
        // Close button position not set yet
        return;
    }

    display->setColor(WHITE);

    if (selected) {
        // Draw highlighted close button background
        display->drawRect(closeButtonX - 1, closeButtonY - 1, closeButtonWidth + 2, closeButtonHeight + 2);
        display->fillRect(closeButtonX, closeButtonY, closeButtonWidth, closeButtonHeight);
        display->setColor(BLACK);
    }

    // Draw the X symbol
    display->drawLine(closeButtonX, closeButtonY, closeButtonX + closeButtonWidth, closeButtonY + closeButtonHeight);
    display->drawLine(closeButtonX + closeButtonWidth, closeButtonY, closeButtonX, closeButtonY + closeButtonHeight);

    // Reset color
    display->setColor(WHITE);
}

void VirtualKeyboard::drawInputArea(OLEDDisplay *display, int16_t offsetX, int16_t offsetY)
{
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    int screenWidth = display->getWidth();

    int headerHeight = 0;
    if (!headerText.empty()) {
        display->drawString(offsetX + 2, offsetY, headerText.c_str());

        // Set close button position
        closeButtonX = screenWidth - 12;
        closeButtonY = offsetY;
        closeButtonWidth = 8;
        closeButtonHeight = 8;

        drawCloseButton(display, offsetX, offsetY, false);

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
    int textPadding = 6; // 2px left padding + 4px right padding for cursor space
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
            // Recalculate width with ellipsis and ensure it still fits
            textWidth = display->getStringWidth(scrolledText.c_str());
            while (textWidth > maxWidth && scrolledText.length() > 3) {
                // Remove one character after "..." and recalculate
                scrolledText = "..." + scrolledText.substr(4);
                textWidth = display->getStringWidth(scrolledText.c_str());
            }
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
        // Ensure cursor stays within box bounds with proper margin from right edge
        int rightBoundary = offsetX + 2 + boxWidth - 3; // 3px margin from right border
        if (cursorX < rightBoundary) {
            // Align cursor properly with the text baseline and height - moved down by 2 pixels
            display->drawVerticalLine(cursorX, textY + 2, 10);
        }
    }
}

void VirtualKeyboard::drawKey(OLEDDisplay *display, const VirtualKey &key, bool selected, int16_t offsetX, int16_t offsetY)
{
    int x = offsetX + key.x;
    int y = offsetY + key.y;

    // Draw border for OK key or selected keys (NOT for backspace key)
    bool drawBorder = selected || (key.type == VK_ENTER);

    if (drawBorder) {
        if (selected) {
            if (key.type == VK_BACKSPACE) {
                display->fillRect(x, y + 3, key.width, 10);
            } else if (key.type == VK_ENTER) {
                display->fillRect(x, y + 3, key.width, 10);
            } else {
                display->fillRect(x, y + 3, key.width, key.height);
            }
            display->setColor(BLACK);
        } else {
            display->setColor(WHITE);
        }
    } else {
        display->setColor(WHITE);
    }

    // Draw key content
    display->setFont(FONT_SMALL);

    if (key.type == VK_BACKSPACE) {
        int centerX = x + key.width / 2;
        int centerY = y + key.height / 2;

        display->drawLine(centerX - 3, centerY + 1, centerX + 2, centerY + 1); // horizontal line
        display->drawLine(centerX - 3, centerY + 1, centerX - 1, centerY - 1); // upper diagonal
        display->drawLine(centerX - 3, centerY + 1, centerX - 1, centerY + 3); // lower diagonal
    } else if (key.type == VK_ENTER) {
        std::string keyText = "OK";
        int textWidth = display->getStringWidth(keyText.c_str());
        int textX = x + (key.width - textWidth) / 2;
        int textY = y + 2;
        display->drawString(textX, textY - 1, keyText.c_str());
        display->drawRect(textX - 1, textY, textWidth + 3, 11);
    } else {
        std::string keyText;
        char c = getCharForKey(key, false); // Pass false for display purposes

        if (key.character == ' ') {
            keyText = "_"; // Show underscore for space
        } else if (key.character == '_') {
            keyText = "_"; // Show underscore for underscore character
        } else {
            keyText = c;
        }

        // Center text in key with perfect horizontal and vertical alignment
        int textWidth = display->getStringWidth(keyText.c_str());
        int textX = x + (key.width - textWidth) / 2;
        int textY = y; // Fixed position for optimal centering in 12px height

        // If the character is a digit, adjust X position by +1
        if (key.character >= '0' && key.character <= '9') {
            textX += 1;
            textY += 1;
        }

        display->drawString(textX, textY + 1, keyText.c_str());
    }

    // Reset color after drawing
    if (selected) {
        display->setColor(WHITE);
    }
}

char VirtualKeyboard::getCharForKey(const VirtualKey &key, bool isLongPress)
{
    if (key.type != VK_CHAR) {
        return key.character;
    }

    char c = key.character;

    if (isLongPress) {
        if (c == '_') {
            return ' ';
        } else if (c == '.') {
            return ',';
        } else if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
    }

    return c;
}

void VirtualKeyboard::moveCursorUp()
{
    resetTimeout();

    // If we're on the close button, move to keyboard
    if (cursorOnCloseButton) {
        cursorOnCloseButton = false;
        cursorRow = 0;
        cursorCol = KEYBOARD_COLS - 1; // Move to rightmost key in top row
        return;
    }

    uint8_t originalRow = cursorRow;
    if (cursorRow > 0) {
        cursorRow--;
    } else {
        // From top row, move to close button if on rightmost position
        if (cursorCol >= KEYBOARD_COLS - 3) { // Close to right edge
            cursorOnCloseButton = true;
            return;
        }
        cursorRow = KEYBOARD_ROWS - 1;
    }

    // If the new position is empty, find the nearest valid key in this row
    if (keyboard[cursorRow][cursorCol].character == 0) {
        // First try to move left to find a valid key
        uint8_t originalCol = cursorCol;
        while (cursorCol > 0 && keyboard[cursorRow][cursorCol].character == 0) {
            cursorCol--;
        }
        // If we still don't have a valid key, try moving right from original position
        if (keyboard[cursorRow][cursorCol].character == 0) {
            cursorCol = originalCol;
            while (cursorCol < KEYBOARD_COLS - 1 && keyboard[cursorRow][cursorCol].character == 0) {
                cursorCol++;
            }
        }
        // If still no valid key, go back to original row
        if (keyboard[cursorRow][cursorCol].character == 0) {
            cursorRow = originalRow;
        }
    }
}

void VirtualKeyboard::moveCursorDown()
{
    resetTimeout();

    uint8_t originalRow = cursorRow;
    if (cursorRow < KEYBOARD_ROWS - 1) {
        cursorRow++;
    } else {
        cursorRow = 0;
    }

    // If the new position is empty, find the nearest valid key in this row
    if (keyboard[cursorRow][cursorCol].character == 0) {
        // First try to move left to find a valid key
        uint8_t originalCol = cursorCol;
        while (cursorCol > 0 && keyboard[cursorRow][cursorCol].character == 0) {
            cursorCol--;
        }
        // If we still don't have a valid key, try moving right from original position
        if (keyboard[cursorRow][cursorCol].character == 0) {
            cursorCol = originalCol;
            while (cursorCol < KEYBOARD_COLS - 1 && keyboard[cursorRow][cursorCol].character == 0) {
                cursorCol++;
            }
        }
        // If still no valid key, go back to original row
        if (keyboard[cursorRow][cursorCol].character == 0) {
            cursorRow = originalRow;
        }
    }
}

void VirtualKeyboard::moveCursorLeft()
{
    resetTimeout();

    // Find the previous valid key position
    do {
        if (cursorCol > 0) {
            cursorCol--;
        } else {
            if (cursorRow > 0) {
                cursorRow--;
                cursorCol = KEYBOARD_COLS - 1;
            } else {
                cursorRow = KEYBOARD_ROWS - 1;
                cursorCol = KEYBOARD_COLS - 1;
            }
        }
    } while ((keyboard[cursorRow][cursorCol].character == 0 && keyboard[cursorRow][cursorCol].type == VK_CHAR) &&
             !(cursorRow == 0 && cursorCol == 0)); // Prevent infinite loop
}

void VirtualKeyboard::moveCursorRight()
{
    resetTimeout();

    // If we're on the close button, go back to keyboard
    if (cursorOnCloseButton) {
        cursorOnCloseButton = false;
        cursorRow = 0;
        cursorCol = 0;
        return;
    }

    // Find the next valid key position
    do {
        if (cursorCol < KEYBOARD_COLS - 1) {
            cursorCol++;
        } else {
            // From top row's rightmost position, check if we should go to close button
            if (cursorRow == 0) {
                cursorOnCloseButton = true;
                return;
            }

            if (cursorRow < KEYBOARD_ROWS - 1) {
                cursorRow++;
                cursorCol = 0;
            } else {
                cursorRow = 0;
                cursorCol = 0;
            }
        }
    } while ((keyboard[cursorRow][cursorCol].character == 0 && keyboard[cursorRow][cursorCol].type == VK_CHAR) &&
             !(cursorRow == 0 && cursorCol == 0)); // Prevent infinite loop
}

void VirtualKeyboard::handlePress()
{
    resetTimeout(); // Reset timeout on any input activity

    // Handle close button press
    if (cursorOnCloseButton) {
        LOG_INFO("Virtual keyboard: close button pressed, cancelling");
        if (onTextEntered) {
            // Store callback before clearing to prevent use-after-free
            std::function<void(const std::string &)> callback = onTextEntered;
            onTextEntered = nullptr; // Clear immediately to prevent re-entry
            inputText = "";          // Clear input

            // Call callback with empty string to signal cancellation
            callback("");
        }
        return;
    }

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
    default:
        break;
    }
}

void VirtualKeyboard::handleLongPress()
{
    resetTimeout(); // Reset timeout on any input activity

    // Handle close button long press (same as regular press for now)
    if (cursorOnCloseButton) {
        // Call callback with empty string to indicate cancel/close
        if (onTextEntered) {
            onTextEntered("");
        }
        return;
    }

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

bool VirtualKeyboard::isCursorOnCloseButton() const
{
    return cursorOnCloseButton;
}

} // namespace graphics
