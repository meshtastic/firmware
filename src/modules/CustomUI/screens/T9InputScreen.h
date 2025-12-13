#pragma once

#include "BaseScreen.h"
#include <Arduino.h>
#include <functional>

/**
 * T9 Input Screen - Text input using traditional T9 multi-tap method
 * Features:
 * - T9 character mapping: 2=ABC, 3=DEF, 4=GHI, 5=JKL, 6=MNO, 7=PQRS, 8=TUV, 9=WXYZ, 0=space
 * - Multi-tap input with timeout for character acceptance
 * - Visual feedback showing current character and full message
 * - Navigation: [A] Back/Cancel, [#] Confirm/Send, [*] Backspace
 * - Callback function support for flexible response handling
 */
class T9InputScreen : public BaseScreen {
public:
    // Callback function type for when user confirms input
    using ConfirmCallback = std::function<void(const String& text)>;
    
    T9InputScreen();
    virtual ~T9InputScreen();

    // BaseScreen interface
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    virtual bool handleKeyPress(char key) override;
    virtual bool needsUpdate() const override;

    /**
     * Set the callback function to be called when user confirms input
     * @param callback Function to call with the entered text
     */
    void setConfirmCallback(ConfirmCallback callback);
    
    /**
     * Clear the input text and reset state
     */
    void clearInput();
    
    /**
     * Set initial text (for editing existing text)
     * @param text Initial text to display
     */
    void setInitialText(const String& text);
    
    /**
     * Get current input text
     * @return current text being typed
     */
    String getCurrentText() const;

private:
    // T9 character mapping
    static const char* T9_MAP[10];
    static const int T9_LENGTHS[10];
    
    // Input timing constants
    static const unsigned long CHAR_TIMEOUT = 1000;  // 1 second to accept character
    static const int MAX_INPUT_LENGTH = 150;          // Maximum message length
    
    // Layout constants
    static const int INPUT_AREA_HEIGHT = 80;         // Height for input display (increased)
    static const int CHAR_PREVIEW_HEIGHT = 30;       // Height for current character preview
    static const int TEXT_MARGIN = 10;               // Left margin for text
    static const int HEADER_HEIGHT = 35;             // Height for Message: label (new)
    
    // Input state
    String inputText;                    // Current input text
    char currentKey;                     // Key currently being pressed
    int currentKeyPresses;               // Number of times current key was pressed
    unsigned long lastKeyTime;           // Time of last key press
    bool hasCurrentChar;                 // True if we're building a character
    
    // Callback
    ConfirmCallback onConfirm;
    
    // Display state - dirty rectangle optimization
    bool inputDirty;                     // Input area needs redraw
    bool charPreviewDirty;               // Character preview needs redraw
    bool headerDirty;                    // Header/label area needs redraw
    bool fullRedrawNeeded;               // Full screen redraw needed
    
    /**
     * Process character timeout - accept current character and add to input
     */
    void processCharacterTimeout();
    
    /**
     * Get character from T9 mapping
     * @param key The key pressed ('2'-'9')
     * @param presses Number of times key was pressed (0-based)
     * @return character, or '\0' if invalid
     */
    char getT9Character(char key, int presses);
    
    /**
     * Add character to input text
     * @param ch Character to add
     */
    void addCharacter(char ch);
    
    /**
     * Remove last character from input text
     */
    void backspace();
    
    /**
     * Handle T9 key press (2-9, 0 for space)
     * @param key The key pressed
     */
    void handleT9Key(char key);
    
    /**
     * Accept current character being built and add to input
     */
    void acceptCurrentCharacter();
    
    /**
     * Update navigation hints based on current state
     */
    void updateNavigationHints();
    
    /**
     * Draw the input text area
     */
    void drawInputArea(lgfx::LGFX_Device& tft);
    
    /**
     * Draw current character preview (what's being typed)
     */
    void drawCharacterPreview(lgfx::LGFX_Device& tft);
    
    /**
     * Draw the header/label area ("Message:")
     */
    void drawHeaderArea(lgfx::LGFX_Device& tft);
    
    /**
     * Clear specific region without affecting others
     */
    void clearRegion(lgfx::LGFX_Device& tft, int x, int y, int width, int height);
    
    /**
     * Draw text with word wrapping in specified area
     * @param tft Display device
     * @param text Text to draw
     * @param x X position
     * @param y Y position
     * @param maxWidth Maximum width before wrapping
     * @param maxHeight Maximum height for text area
     * @param textSize Font size
     * @return Y position after last line
     */
    int drawWrappedText(lgfx::LGFX_Device& tft, const String& text, int x, int y, 
                       int maxWidth, int maxHeight, int textSize = 1);
    
    // Colors
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_WHITE = 0xFFFF;
    static const uint16_t COLOR_GREEN = 0x07E0;      // Confirm button
    static const uint16_t COLOR_YELLOW = 0xFFE0;     // Character preview
    static const uint16_t COLOR_BLUE = 0x001F;       // Input text
    static const uint16_t COLOR_GRAY = 0x8410;       // Instructions
    static const uint16_t COLOR_RED = 0xF800;        // Cancel/Backspace
};