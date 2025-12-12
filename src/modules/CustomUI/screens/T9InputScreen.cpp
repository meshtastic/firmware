#include "T9InputScreen.h"
#include "BaseScreen.h"
#include "configuration.h"

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

// T9 character mapping: index corresponds to number of key presses (0-based)
const char* T9InputScreen::T9_MAP[10] = {
    " ",           // 0: space
    "",            // 1: (not used in T9)
    "abc",         // 2: ABC
    "def",         // 3: DEF
    "ghi",         // 4: GHI
    "jkl",         // 5: JKL
    "mno",         // 6: MNO
    "pqrs",        // 7: PQRS
    "tuv",         // 8: TUV
    "wxyz"         // 9: WXYZ
};

const int T9InputScreen::T9_LENGTHS[10] = {
    1, 0, 3, 3, 3, 3, 3, 4, 3, 4  // Number of characters for each key
};

T9InputScreen::T9InputScreen() 
    : BaseScreen("T9 Input"), currentKey('\0'), currentKeyPresses(0), 
      lastKeyTime(0), hasCurrentChar(false), inputDirty(true), 
      charPreviewDirty(true) {
    
    updateNavigationHints();
    LOG_INFO("📱 T9InputScreen: Initialized with T9 character mapping");
}

T9InputScreen::~T9InputScreen() {
    clearInput();
}

void T9InputScreen::onEnter() {
    LOG_INFO("📱 T9InputScreen: Entering T9 input mode");
    
    // Reset input state
    currentKey = '\0';
    currentKeyPresses = 0;
    lastKeyTime = 0;
    hasCurrentChar = false;
    
    // Mark everything for redraw
    inputDirty = true;
    charPreviewDirty = true;
    
    updateNavigationHints();
    forceRedraw();
}

void T9InputScreen::onExit() {
    LOG_INFO("📱 T9InputScreen: Exiting T9 input mode");
    
    // Accept any pending character
    if (hasCurrentChar) {
        acceptCurrentCharacter();
    }
    
    // Don't clear callback - it should persist for screen lifetime
    // onConfirm = nullptr;
}

void T9InputScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Check for character timeout
    if (hasCurrentChar && (millis() - lastKeyTime > CHAR_TIMEOUT)) {
        processCharacterTimeout();
    }
    
    // Draw input area
    if (inputDirty) {
        drawInputArea(tft);
        inputDirty = false;
    }
    
    // Draw character preview
    if (charPreviewDirty) {
        drawCharacterPreview(tft);
        charPreviewDirty = false;
    }
}

bool T9InputScreen::handleKeyPress(char key) {
    LOG_INFO("📱 T9InputScreen: Key pressed: %c", key);
    
    switch (key) {
        case 'A':
        case 'a':
            // Cancel/Back - don't call callback, just return false to let module handle navigation
            return false;
            
        case '#':
            // Confirm/Send
            LOG_INFO("📱 T9InputScreen: # pressed - hasCurrentChar: %s, inputText length: %d, onConfirm: %s", 
                     hasCurrentChar ? "true" : "false", inputText.length(), onConfirm ? "set" : "null");
            
            if (hasCurrentChar) {
                acceptCurrentCharacter();
            }
            
            LOG_INFO("📱 T9InputScreen: After accepting char - inputText length: %d", inputText.length());
            
            if (onConfirm && inputText.length() > 0) {
                String finalText = inputText;
                LOG_INFO("📱 T9InputScreen: Confirming input: '%s'", finalText.c_str());
                onConfirm(finalText);
            } else {
                LOG_INFO("📱 T9InputScreen: Cannot confirm - onConfirm: %s, text length: %d", 
                         onConfirm ? "set" : "null", inputText.length());
            }
            return false; // Let module handle screen switch
            
        case '*':
            // Backspace
            if (hasCurrentChar) {
                // Cancel current character being built
                hasCurrentChar = false;
                currentKey = '\0';
                currentKeyPresses = 0;
                inputDirty = true;
                forceRedraw(); // Need this for screen update
            } else if (inputText.length() > 0) {
                // Remove last character from input
                backspace();
            }
            return true;
            
        case '0':
            // Space key
            if (hasCurrentChar) {
                acceptCurrentCharacter();
            }
            addCharacter(' ');
            return true;
            
        case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
            // T9 input keys
            handleT9Key(key);
            return true;
            
        default:
            // Other keys not handled
            return false;
    }
}

void T9InputScreen::setConfirmCallback(ConfirmCallback callback) {
    onConfirm = callback;
    LOG_INFO("📱 T9InputScreen: Callback set");
}

void T9InputScreen::clearInput() {
    inputText = "";
    currentKey = '\0';
    currentKeyPresses = 0;
    lastKeyTime = 0;
    hasCurrentChar = false;
    
    inputDirty = true;
    charPreviewDirty = true;
    updateNavigationHints();
    forceRedraw(); // Force immediate visual update
    
    LOG_INFO("📱 T9InputScreen: Input cleared");
}

void T9InputScreen::setInitialText(const String& text) {
    if (text.length() <= MAX_INPUT_LENGTH) {
        inputText = text;
        inputDirty = true;
        updateNavigationHints();
        LOG_INFO("📱 T9InputScreen: Initial text set: '%s'", text.c_str());
    }
}

String T9InputScreen::getCurrentText() const {
    return inputText;
}

void T9InputScreen::processCharacterTimeout() {
    if (hasCurrentChar) {
        acceptCurrentCharacter();
        LOG_INFO("📱 T9InputScreen: Character timeout - accepted character");
        // Force immediate visual update after timeout
        forceRedraw();
    }
}

char T9InputScreen::getT9Character(char key, int presses) {
    if (key < '0' || key > '9') {
        return '\0';
    }
    
    int keyIndex = key - '0';
    if (keyIndex < 0 || keyIndex >= 10) {
        return '\0';
    }
    
    const char* chars = T9_MAP[keyIndex];
    int maxPresses = T9_LENGTHS[keyIndex];
    
    if (presses < 0 || presses >= maxPresses || chars == nullptr) {
        return '\0';
    }
    
    return chars[presses];
}

void T9InputScreen::addCharacter(char ch) {
    if (inputText.length() < MAX_INPUT_LENGTH && ch != '\0') {
        inputText += ch;
        inputDirty = true;
        updateNavigationHints();
        forceRedraw(); // Force immediate visual update
        LOG_INFO("📱 T9InputScreen: Added character: '%c', text now: '%s'", ch, inputText.c_str());
    }
}

void T9InputScreen::backspace() {
    if (inputText.length() > 0) {
        inputText.remove(inputText.length() - 1);
        inputDirty = true;
        updateNavigationHints();
        forceRedraw(); // Need this for screen update
        LOG_INFO("📱 T9InputScreen: Backspace, text now: '%s'", inputText.c_str());
    }
}

void T9InputScreen::handleT9Key(char key) {
    unsigned long currentTime = millis();
    
    // Check if this is the same key pressed recently
    if (hasCurrentChar && currentKey == key && (currentTime - lastKeyTime < CHAR_TIMEOUT)) {
        // Same key pressed within timeout - cycle to next character
        currentKeyPresses++;
        int maxPresses = T9_LENGTHS[key - '0'];
        if (currentKeyPresses >= maxPresses) {
            currentKeyPresses = 0; // Wrap around
        }
        LOG_INFO("📱 T9InputScreen: Same key cycled, presses: %d", currentKeyPresses);
    } else {
        // Different key or timeout exceeded - accept previous char and start new one
        if (hasCurrentChar) {
            acceptCurrentCharacter();
        }
        
        currentKey = key;
        currentKeyPresses = 0;
        hasCurrentChar = true;
        LOG_INFO("📱 T9InputScreen: New key started: %c", key);
    }
    
    lastKeyTime = currentTime;
    
    // Force immediate visual update for responsive feedback
    inputDirty = true;
    charPreviewDirty = true;
    forceRedraw();
}

void T9InputScreen::acceptCurrentCharacter() {
    if (hasCurrentChar) {
        char ch = getT9Character(currentKey, currentKeyPresses);
        if (ch != '\0') {
            addCharacter(ch);
        }
        
        hasCurrentChar = false;
        currentKey = '\0';
        currentKeyPresses = 0;
        
        // Force redraw of both areas when character is committed
        inputDirty = true;
        charPreviewDirty = true;
        forceRedraw();
        
        LOG_INFO("📱 T9InputScreen: Accepted character");
    }
}

void T9InputScreen::updateNavigationHints() {
    navHints.clear();
    navHints.push_back(NavHint('*', "Del"));
    navHints.push_back(NavHint('#', "Send"));
    navHints.push_back(NavHint('A', "Cancel"));
}

void T9InputScreen::drawInputArea(lgfx::LGFX_Device& tft) {
    int inputY = getContentY() + 5;
    
    // Clear input area
    tft.fillRect(0, inputY, getContentWidth(), INPUT_AREA_HEIGHT, COLOR_BLACK);
    
    // Draw input label
    tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft.setTextSize(1);
    tft.setCursor(TEXT_MARGIN, inputY + 5);
    tft.print("Message:");
    
    // Draw input text with wrapping
    tft.setTextColor(COLOR_BLUE, COLOR_BLACK);
    tft.setTextSize(1);
    
    // Show committed text + current character being typed
    String displayText = inputText;
    
    // Add current character if building one
    if (hasCurrentChar) {
        char previewChar = getT9Character(currentKey, currentKeyPresses);
        if (previewChar != '\0') {
            displayText += previewChar;
        }
    }
    
    // Draw text with wrapping
    drawWrappedText(tft, displayText, TEXT_MARGIN, inputY + 20, 
                    getContentWidth() - TEXT_MARGIN * 2, INPUT_AREA_HEIGHT - 25, 1);
    
    LOG_INFO("📱 T9InputScreen: Drew input area");
}

void T9InputScreen::drawCharacterPreview(lgfx::LGFX_Device& tft) {
    int previewY = getContentY() + INPUT_AREA_HEIGHT + 10;
    
    // Clear character preview area
    tft.fillRect(0, previewY, getContentWidth(), CHAR_PREVIEW_HEIGHT, COLOR_BLACK);
    
    // No character hints displayed - clean interface
    
    LOG_INFO("📱 T9InputScreen: Drew character preview");
}

int T9InputScreen::drawWrappedText(lgfx::LGFX_Device& tft, const String& text, int x, int y, 
                                 int maxWidth, int maxHeight, int textSize) {
    if (text.length() == 0) {
        return y;
    }
    
    tft.setTextSize(textSize);
    
    const int charWidth = 6 * textSize;  // Approximate character width
    const int lineHeight = 8 * textSize; // Approximate line height
    const int maxCharsPerLine = maxWidth / charWidth;
    
    int currentY = y;
    int start = 0;
    int length = text.length();
    
    while (start < length && (currentY - y + lineHeight) <= maxHeight) {
        int end = start + maxCharsPerLine;
        
        if (end >= length) {
            // Last line
            String line = text.substring(start);
            tft.setCursor(x, currentY);
            tft.print(line);
            currentY += lineHeight;
            break;
        }
        
        // Find word boundary
        int lastSpace = -1;
        for (int i = std::min(end, length - 1); i >= start; i--) {
            if (text.charAt(i) == ' ') {
                lastSpace = i;
                break;
            }
        }
        
        if (lastSpace > start && (lastSpace - start) >= (maxCharsPerLine / 2)) {
            // Good break point
            String line = text.substring(start, lastSpace);
            tft.setCursor(x, currentY);
            tft.print(line);
            start = lastSpace + 1;
        } else {
            // Break at character limit
            String line = text.substring(start, end);
            tft.setCursor(x, currentY);
            tft.print(line);
            start = end;
        }
        
        currentY += lineHeight;
    }
    
    return currentY;
}