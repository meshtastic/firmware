#include "modules/MorseInputModule.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/SharedUIDisplay.h"
#include "input/ButtonThread.h"
#include <Arduino.h>

extern ButtonThread *UserButtonThread;
extern graphics::Screen *screen;

namespace graphics
{

struct MorseChar {
    char c;
    const char *code;
};

static const MorseChar morseTable[] = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},    {'F', "..-."},
    {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."},
    {'M', "--"},   {'N', "-."},   {'O', "---"},  {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},
    {'S', "..."},  {'T', "-"},    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'0', "-----"},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'\'', ".----."}, {'!', "-.-.--"}, {'/', "-..-."},
    {'(', "-.--."}, {')', "-.--.-"}, {'&', ".-..."}, {':', "---..."}, {';', "-.-.-."}, {'=', "-...-"},
    {'+', ".-.-."}, {'-', "-....-"}, {'_', "..--.-"}, {'"', ".-..-."}, {'$', "...-..-"}, {'@', ".--.-."},
    {'\b', "........"}, /* correction --> backspace */
    {'\n', ".-.-."}     /* OUT --> send message */
};

MorseInputModule &MorseInputModule::instance()
{
    static MorseInputModule inst;
    return inst;
}

MorseInputModule::MorseInputModule() : concurrency::OSThread("MorseInput") {}

void MorseInputModule::start(const char *header, const char *initialText, uint32_t durationMs, std::function<void(const std::string &)> cb)
{
    active = true;
    headerText = header ? header : "Morse Input";
    inputText = initialText ? initialText : "";
    callback = cb;
    currentMorse = "";
    lastInputTime = millis();
    buttonPressed = false;
    ignoreRelease = false;
    menuOpen = false;
    charPickerOpen = false;
    shift = false;
    autoShift = true;
    consecutiveDots = 0;
    
    if (inputText.empty()) {
        shift = true;
    } else {
        char last = inputText.back();
        if (last == '.' || last == '!' || last == '?') {
            shift = true;
        }
    }
    
    waitForRelease = true;
    setIntervalFromNow(20);
}

void MorseInputModule::stop(bool callEmptyCallback)
{
    active = false;
    if (callEmptyCallback && callback) {
        callback("");
    }
    callback = nullptr;
}

bool MorseInputModule::handleInput(const InputEvent &event)
{
    if (!active) return false;
    
    // We handle button input via polling in runOnce, so we consume button events here to prevent other handlers from seeing them
    if (event.inputEvent == INPUT_BROKER_USER_PRESS || 
        event.inputEvent == INPUT_BROKER_SELECT || 
        event.inputEvent == INPUT_BROKER_SELECT_LONG) {
        return true;
    }
    
    return false;
}

int32_t MorseInputModule::runOnce()
{
    if (!active) return 100;
    
    if (!UserButtonThread) return 100;

    bool pressed = UserButtonThread->isHeld();
    
    if (waitForRelease) {
        if (!pressed) {
            waitForRelease = false;
        }
        return 20;
    }

    uint32_t now = millis();

    if (pressed && !buttonPressed) {
        // Press started
        buttonPressed = true;
        buttonPressTime = now;
        if (screen) screen->onPress();
    } else if (!pressed && buttonPressed) {
        // Press released
        buttonPressed = false;
        if (screen) screen->onPress();
        if (!ignoreRelease) {
            uint32_t duration = now - buttonPressTime;
            if (menuOpen) {
                // Menu navigation - Short press (or release before hold threshold)
                menuSelection = (menuSelection + 1) % 6;

                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
            } else if (charPickerOpen) {
                 // Char picker navigation
                 if (duration > 2000) { // Extra long press
                    // Back
                    charPickerOpen = false;
                 } else if (duration > 500) { // Long press
                     // Select
                     static const char chars[] = "'!/-()&:;=+\"-_$@";
                     if (charPickerSelection >= 0 && charPickerSelection < (int)strlen(chars)) {
                         inputText += chars[charPickerSelection];
                     }
                     charPickerOpen = false;
                 } else {
                     // Next
                     static const char chars[] = "'!/-()&:;=+\"-_$@";
                     charPickerSelection = (charPickerSelection + 1) % strlen(chars);
                 }
                 UIFrameEvent e;
                 e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                 notifyObservers(&e);
            } else {
                // Morse input
                // Fixed timing: < 300ms = Dot, > 300ms = Dash
                // Menu opens at 2000ms, so Dash is 300ms - 2000ms
                if (duration < 300) { // Dot
                    consecutiveDots++;
                     if (consecutiveDots == 8) {
                        // Check if sequence has dashes
                        if (currentMorse.find('-') == std::string::npos) {
                            // Pure dots -> Backspace immediately
                            if (!inputText.empty()) {
                                inputText.pop_back();
                            }
                        }
                        // Always cancel the current sequence
                        currentMorse = ""; 
                        // Don't restart new sequence yet, wait for consecutiveDots to reset
                     } else if (consecutiveDots > 8) {
                        currentMorse = ""; // Continue ignoring
                     } else {
                        currentMorse += "."; 
                     }
                } else { // Dash
                    consecutiveDots = 0;
                    currentMorse += "-";
                }
                lastInputTime = now;
                
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
            }
        }
        ignoreRelease = false;
    } else if (pressed && buttonPressed) {
        // Button is being held
        uint32_t duration = now - buttonPressTime;

        if (menuOpen) {
            if (duration > 500) { // Long press -> Select immediately
                // Select item
                switch (menuSelection) {
                    case 0: // Back
                        menuOpen = false;
                        break;
                    case 1: // Backspace
                        if (!inputText.empty()) {
                            inputText.pop_back();
                        }
                        // Keep menu open
                        break;
                    case 2: // Shift
                        shift = !shift;
                        menuOpen = false;
                        break;
                    case 3: // Char Picker
                        menuOpen = false;
                        charPickerOpen = true;
                        charPickerSelection = 0;
                        break;
                    case 4: // Send
                        if (callback) callback(inputText);
                        stop();
                        break;
                    case 5: // Exit
                        stop(true);
                        break;
                }
                
                ignoreRelease = true;
                waitForRelease = true;
                
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
                return 20;
            }
        } else if (!charPickerOpen) {
            // Force update when crossing dot/dash threshold
            if (duration >= 300 && duration < 340) {
                 UIFrameEvent e;
                 e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                 notifyObservers(&e);
            }
        }

        if (!menuOpen && !charPickerOpen && duration > 2000) {
             // Open menu
             menuOpen = true;
             menuSelection = 0;
             ignoreRelease = true;
             waitForRelease = true;
             
             UIFrameEvent e;
             e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
             notifyObservers(&e);
        }
    } else {
        // Idle
        if (!menuOpen && !charPickerOpen && !currentMorse.empty()) {
            // Auto-commit character (fixed timing)
            if (now - lastInputTime > 1000) {
                commitCharacter();
                consecutiveDots = 0;
            }
        } else if (!menuOpen && !charPickerOpen && currentMorse.empty()) {
             // Reset backspace tracking if enough time has passed
             if (consecutiveDots > 0 && now - lastInputTime > 1000) {
                 consecutiveDots = 0;
             }
             
            // Auto-space
            if (now - lastInputTime > 3000 && !inputText.empty() && inputText.back() != ' ') {
                inputText += " ";
                lastInputTime = now;
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
            }
        }
    }

    return 20;
}

void MorseInputModule::commitCharacter()
{
    char c = morseToChar(currentMorse);
    if (c == '\b') {
        if (!inputText.empty()) {
            inputText.pop_back();
        }
    } else if (c == '\n') {
        if (callback) callback(inputText);
        stop();
        return;
    } else if (c != 0) {
        if (shift) {
            c = toupper(c);
            if (autoShift) shift = false;
        } else {
            c = tolower(c);
        }
        inputText += c;
        
        // Check for auto-shift
        if (c == '.' || c == '!' || c == '?') {
            shift = true;
        }
    }
    
    currentMorse = "";
    lastInputTime = millis();
    
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
}

char MorseInputModule::morseToChar(const std::string &code)
{
    for (const auto &mc : morseTable) {
        if (code == mc.code) {
            return mc.c;
        }
    }
    return 0;
}

void MorseInputModule::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!active) return;

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }
    
    if (charPickerOpen) {
        drawCharPicker(display, x, y);
        return;
    }

    drawMorseInterface(display, x, y);
}

void MorseInputModule::drawMorseInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    std::string activeMorse = currentMorse;
    if (buttonPressed && !menuOpen && !charPickerOpen) {
        uint32_t duration = millis() - buttonPressTime;
        if (duration >= 300) {
            activeMorse += "-";
        } else {
            activeMorse += ".";
        }
    }

    // Input Text
    int lineHeight = 10;
    int currentY = y;
    
    std::string displayInput = inputText;
    if ((millis() / 500) % 2 == 0) {
        displayInput += "_";
    }
    
    // Handle scrolling if text is too long
    int width = display->getStringWidth(displayInput.c_str());
    int maxWidth = display->getWidth();
    if (width > maxWidth) {
        int charWidth = 6; 
        int maxChars = maxWidth / charWidth;
        if (displayInput.length() > (size_t)maxChars) {
            displayInput = "..." + displayInput.substr(displayInput.length() - maxChars + 3);
        }
    }
    
    display->drawString(x, currentY, displayInput.c_str());
    
    // Horizontal Line
    currentY += lineHeight;
    currentY += 3; // Spacing
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 2; // Spacing

    // Character Layout
    const char *rows[] = {
        "ABCD EFGH IJKL MNOP QRST",
        "UVW XYZ ,.? 0123 456 789"
    };
    
    int startX = x;
    int charSpacing = 5; 
    
    for (int r = 0; r < 2; ++r) {
        const char *layout = rows[r];
        int currentX = startX;
        
        for (int i = 0; layout[i]; ++i) {
            char c = layout[i];

            if (c == ' ') {
                currentX += charSpacing;
                continue;
            }

            std::string code = "";
            for (const auto &mc : morseTable) {
                if (mc.c == c) {
                    code = mc.code;
                    break;
                }
            }
            
            bool isVisible = false;
            char hintChar = ' ';
            bool isSelected = false;
            
            // Check if this character matches the current input prefix
            if (code.find(activeMorse) == 0) {
                isVisible = true;
                if (code == activeMorse) {
                    isSelected = true; // Complete match
                } else {
                    char next = code[activeMorse.length()];
                    hintChar = next;
                }
            }
            
            // If visible, draw it. If not, we skip drawing but keep the space (currentX still increments)
            if (isVisible) {
                char displayChar = (shift ? c : tolower(c));
                // Numbers and punctuation don't have lower case in this context, but good to be safe
                if (!isalpha(c)) displayChar = c;
                
                // Draw hint (dot/dash) if not selected
                if (!isSelected && hintChar != ' ') {
                    if (hintChar == '.') {
                        int w = 3; 
                        int h = 3;
                        display->fillRect(currentX + (charSpacing - w) / 2, currentY + 8, w, h);
                    } else if (hintChar == '-') {
                        int w = 5;
                        int h = 2; // Slightly thinner dash
                        display->fillRect(currentX + (charSpacing - w) / 2, currentY + 6, w, h);
                    }
                }
                
                // Draw char
                std::string ch(1, displayChar);
                if (isSelected) {
                    // Draw inverted (negative) for selected char
                    int w = charSpacing; // Or use display->getStringWidth(ch.c_str()) + padding
                    if (w < 6) w = 6;
                    int h = lineHeight;
                    // Center the box horizontally
                    int boxX = currentX + (charSpacing - w) / 2;
                    
                    display->fillRect(boxX, currentY + lineHeight, w, h);
                    display->setColor(BLACK);
                    display->drawString(currentX, currentY + lineHeight, ch.c_str());
                    display->setColor(WHITE);
                } else {
                    display->drawString(currentX, currentY + lineHeight, ch.c_str());
                }
            }
            
            currentX += charSpacing;
        }
        currentY += lineHeight * 2; 
    }
}

void MorseInputModule::drawMenu(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    
    display->drawString(x, y, "Morse Menu");
    display->drawLine(x, y + 12, x + display->getWidth(), y + 12);
    
    const char *items[] = {"Back", "Backspace", "Shift", "Char Picker", "Send", "Exit"};
    int itemCount = 6;
    
    // Calculate visible items based on screen height
    int itemHeight = 12;
    int headerHeight = 14;
    int availableHeight = display->getHeight() - y - headerHeight;
    int visibleItems = availableHeight / itemHeight;
    if (visibleItems < 1) visibleItems = 1;
    
    // Scroll logic
    int startItem = 0;
    if (menuSelection >= visibleItems) {
        startItem = menuSelection - visibleItems + 1;
    }
    int endItem = startItem + visibleItems;
    if (endItem > itemCount) endItem = itemCount;
    
    int currentY = y + headerHeight;
    
    for (int i = startItem; i < endItem; ++i) {
        std::string item = items[i];
        if (i == 2) {
            item += (shift ? ": ON" : ": OFF");
        }
        
        if (i == menuSelection) {
            display->fillRect(x, currentY, display->getWidth(), itemHeight);
            display->setColor(BLACK);
            display->drawString(x + 2, currentY, item.c_str());
            display->setColor(WHITE);
        } else {
            display->drawString(x + 2, currentY, item.c_str());
        }
        currentY += itemHeight;
    }
    
    // Draw scrollbar if needed
    if (itemCount > visibleItems) {
        int scrollBarHeight = availableHeight;
        int scrollBarWidth = 4;
        int scrollBarX = display->getWidth() - scrollBarWidth;
        int scrollBarY = y + headerHeight;
        
        int indicatorHeight = scrollBarHeight * visibleItems / itemCount;
        int indicatorY = scrollBarY + (scrollBarHeight - indicatorHeight) * startItem / (itemCount - visibleItems);
        
        display->drawRect(scrollBarX, scrollBarY, scrollBarWidth, scrollBarHeight);
        display->fillRect(scrollBarX + 1, indicatorY, scrollBarWidth - 2, indicatorHeight);
    }
}

void MorseInputModule::drawCharPicker(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    
    display->drawString(x, y, "Char Picker");
    
    static const char chars[] = "'!/-()&:;=+\"-_$@";
    int len = strlen(chars);
    
    // Show a window around selection
    int start = charPickerSelection - 4;
    if (start < 0) start = 0;
    int end = start + 9;
    if (end > len) end = len;
    
    std::string line = "";
    for (int i = start; i < end; ++i) {
        if (i == charPickerSelection) {
            line += "[";
            line += chars[i];
            line += "]";
        } else {
            line += " ";
            line += chars[i];
            line += " ";
        }
    }
    
    display->drawString(x, y + 20, line.c_str());
    display->drawString(x, y + 40, "Hold=Select >2s=Exit");
}

} // namespace graphics

#endif
