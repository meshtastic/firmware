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
      lastKeyTime(0), hasCurrentChar(false), inputTextDirty(true), 
      labelDirty(true) {
    
    // Set navigation hints using BaseScreen system
    std::vector<NavHint> hints;
    hints.push_back(NavHint('*', "Del"));
    hints.push_back(NavHint('#', "Send"));
    hints.push_back(NavHint('A', "Cancel"));
    setNavigationHints(hints);
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
    
    // Mark content areas for redraw
    inputTextDirty = true;
    labelDirty = true;
    
    // Force full screen redraw including BaseScreen's header/footer
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

bool T9InputScreen::needsUpdate() const {
    // Return true if content areas need updating or if building a character with timeout pending
    bool hasContentUpdates = labelDirty || inputTextDirty;
    bool hasTimeoutPending = hasCurrentChar && (millis() - lastKeyTime < CHAR_TIMEOUT);
    
    return hasContentUpdates || hasTimeoutPending || BaseScreen::needsUpdate();
}

void T9InputScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Check for character timeout
    if (hasCurrentChar && (millis() - lastKeyTime > CHAR_TIMEOUT)) {
        processCharacterTimeout();
    }
    
    // Draw label if dirty
    if (labelDirty) {
        drawMessageLabel(tft);
        labelDirty = false;
    }
    
    // Draw input text if dirty
    if (inputTextDirty) {
        drawInputArea(tft);
        inputTextDirty = false;
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
                inputTextDirty = true; // Update display to remove preview char
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
    
    inputTextDirty = true;
    
    LOG_INFO("📱 T9InputScreen: Input cleared");
}

void T9InputScreen::setInitialText(const String& text) {
    if (text.length() <= MAX_INPUT_LENGTH) {
        inputText = text;
        inputTextDirty = true;
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
        // acceptCurrentCharacter() already sets dirty flags
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
        inputTextDirty = true;
        LOG_INFO("📱 T9InputScreen: Added character: '%c', text now: '%s'", ch, inputText.c_str());
    }
}

void T9InputScreen::backspace() {
    if (inputText.length() > 0) {
        inputText.remove(inputText.length() - 1);
        inputTextDirty = true;
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
    
    // Mark input area for update to show current character being built
    inputTextDirty = true;
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
        
        // Input area already marked dirty by addCharacter
        
        LOG_INFO("📱 T9InputScreen: Accepted character");
    }
}

void T9InputScreen::drawMessageLabel(lgfx::LGFX_Device& tft) {
    // Clear label area in content
    int labelY = getContentY() + 5;
    tft.fillRect(0, labelY, getContentWidth(), 25, COLOR_BLACK);
    
    // Draw "Message:" label
    tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft.setTextSize(2);
    tft.setCursor(TEXT_MARGIN, labelY + 5);
    tft.print("Message:");
    
    LOG_INFO("📱 T9InputScreen: Drew message label");
}

void T9InputScreen::drawInputArea(lgfx::LGFX_Device& tft) {
    // Use BaseScreen's content area
    int textY = getContentY() + 35; // Below "Message:" label
    int textAreaHeight = getContentHeight() - 40; // Leave space for label
    
    // Clear input text area
    tft.fillRect(0, textY, getContentWidth(), textAreaHeight, COLOR_BLACK);
    
    // Draw input text with current character being built
    tft.setTextColor(COLOR_BLUE, COLOR_BLACK);
    tft.setTextSize(2);
    
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
    drawWrappedText(tft, displayText, TEXT_MARGIN, textY, 
                    getContentWidth() - TEXT_MARGIN * 2, textAreaHeight - 10, 2);
    
    LOG_INFO("📱 T9InputScreen: Drew input area");
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