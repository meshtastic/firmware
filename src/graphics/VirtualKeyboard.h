#pragma once

#include "configuration.h"
#include <OLEDDisplay.h>
#include <functional>
#include <string>

namespace graphics
{

enum VirtualKeyType { VK_CHAR, VK_BACKSPACE, VK_ENTER, VK_SHIFT };

struct VirtualKey {
    char character;
    VirtualKeyType type;
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
};

class VirtualKeyboard
{
  public:
    VirtualKeyboard();
    ~VirtualKeyboard();

    void draw(OLEDDisplay *display, int16_t offsetX, int16_t offsetY);
    void setInputText(const std::string &text);
    std::string getInputText() const;
    void setHeader(const std::string &header);
    void setCallback(std::function<void(const std::string &)> callback);

    // Navigation methods for encoder input
    void moveCursorUp();
    void moveCursorDown();
    void moveCursorLeft();
    void moveCursorRight();
    void handlePress();
    void handleLongPress();

    // Timeout management
    void resetTimeout();
    bool isTimedOut() const;

    // Check cursor position for input handling
    bool isCursorOnCloseButton() const;

  private:
    static const uint8_t KEYBOARD_ROWS = 3;
    static const uint8_t KEYBOARD_COLS = 14;
    static const uint8_t KEY_WIDTH = 9;
    static const uint8_t KEY_HEIGHT = 12; // Optimized for FONT_SMALL text with minimal padding
    static const uint8_t KEYBOARD_START_Y = 25;

    VirtualKey keyboard[KEYBOARD_ROWS][KEYBOARD_COLS];

    std::string inputText;
    std::string headerText;
    std::function<void(const std::string &)> onTextEntered;

    uint8_t cursorRow;
    uint8_t cursorCol;

    // Close button position for cursor navigation
    int closeButtonX, closeButtonY, closeButtonWidth, closeButtonHeight;
    bool cursorOnCloseButton;

    // Timeout management for auto-exit
    uint32_t lastActivityTime;
    static const uint32_t TIMEOUT_MS = 60000; // 1 minute timeout

    void initializeKeyboard();
    void drawKey(OLEDDisplay *display, const VirtualKey &key, bool selected, int16_t offsetX, int16_t offsetY);
    void drawInputArea(OLEDDisplay *display, int16_t offsetX, int16_t offsetY);
    void drawCloseButton(OLEDDisplay *display, int16_t offsetX, int16_t offsetY, bool selected);
    void drawCursor(OLEDDisplay *display, int16_t offsetX, int16_t offsetY);

    char getCharForKey(const VirtualKey &key, bool isLongPress = false);
    void insertCharacter(char c);
    void deleteCharacter();
    void submitText();
};

} // namespace graphics
