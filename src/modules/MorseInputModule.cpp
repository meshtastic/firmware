#include "modules/MorseInputModule.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/SharedUIDisplay.h"
#include "input/ButtonThread.h"
#include <Arduino.h>

extern ButtonThread *UserButtonThread;
extern graphics::Screen *screen;

namespace graphics
{

// Morse code lookup table
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
    {'+', ".-.-."}, {'-', "-....-"}, {'_', "..--.-"}, {'"', ".-..-."}, {'$', "...-..-"}, {'@', ".--.-."}
};

MorseInputModule &MorseInputModule::instance()
{
    static MorseInputModule inst;
    return inst;
}

MorseInputModule::MorseInputModule() : concurrency::OSThread("MorseInput") {}

void MorseInputModule::start(const char *header, const char *initialText, uint32_t durationMs,
                             std::function<void(const std::string &)> cb)
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
                // Menu navigation
                if (duration > 500) { // Long press
                    // Select item
                    switch (menuSelection) {
                        case 0: // Char Picker
                            menuOpen = false;
                            charPickerOpen = true;
                            charPickerSelection = 0;
                            break;
                        case 1: // Shift
                            shift = !shift;
                            menuOpen = false;
                            break;
                        case 2: // Backspace
                            if (!inputText.empty()) {
                                inputText.pop_back();
                            }
                            // Keep menu open
                            break;
                        case 3: // Send
                            if (callback) callback(inputText);
                            stop();
                            break;
                        case 4: // Back
                            menuOpen = false;
                            break;
                        case 5: // Stop
                            stop(true);
                            break;
                    }
                } else {
                    // Next item
                    menuSelection = (menuSelection + 1) % 6;
                }
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
            } else if (charPickerOpen) {
                 // Char picker navigation
                 if (duration > 500) { // Long press
                     // Select
                     static const char chars[] = "0123456789.,?'!/-()&:;=+\"-_$@";
                     if (charPickerSelection >= 0 && charPickerSelection < (int)strlen(chars)) {
                         inputText += chars[charPickerSelection];
                     }
                     charPickerOpen = false;
                 } else {
                     // Next
                     static const char chars[] = "0123456789.,?'!/-()&:;=+\"-_$@";
                     charPickerSelection = (charPickerSelection + 1) % strlen(chars);
                 }
                 UIFrameEvent e;
                 e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                 notifyObservers(&e);
            } else {
                // Morse input
                updateTiming(duration);
                if (duration < dotDuration * 2) { // Dot
                    currentMorse += ".";
                } else { // Dash
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
        if (!menuOpen && !charPickerOpen && duration > 800) {
             // Open menu
             menuOpen = true;
             menuSelection = 0;
             ignoreRelease = true; // Don't process release as dash
             
             UIFrameEvent e;
             e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
             notifyObservers(&e);
        }
    } else {
        // Idle
        if (!menuOpen && !charPickerOpen && !currentMorse.empty()) {
            // Auto-commit character
            if (now - lastInputTime > dotDuration * 4) {
                commitCharacter();
            }
        } else if (!menuOpen && !charPickerOpen && currentMorse.empty()) {
            // Auto-space
            if (now - lastInputTime > 3000 && !inputText.empty() && inputText.back() != ' ') {
                inputText += " ";
                lastInputTime = now; // Reset timer
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                notifyObservers(&e);
            }
        }
    }

    return 20;
}

void MorseInputModule::updateTiming(uint32_t pressDuration)
{
    // Simple adaptive timing
    if (pressDuration < dotDuration * 2) {
        // It was a dot, adjust dotDuration towards this duration
        dotDuration = (dotDuration * 3 + pressDuration) / 4;
        if (dotDuration < 50) dotDuration = 50;
        if (dotDuration > 400) dotDuration = 400;
    }
}

void MorseInputModule::commitCharacter()
{
    char c = morseToChar(currentMorse);
    if (c != 0) {
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

    // Input Text (Top)
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
            
            // Check if this character matches the current input prefix
            if (code.find(currentMorse) == 0) {
                isVisible = true;
                if (code == currentMorse) {
                    hintChar = 'v'; // Complete
                } else {
                    char next = code[currentMorse.length()];
                    hintChar = next;
                }
            }
            
            // If visible, draw it. If not, we skip drawing but keep the space (currentX still increments)
            if (isVisible) {
                char displayChar = (shift ? c : tolower(c));
                // Numbers and punctuation don't have lower case in this context, but good to be safe
                if (!isalpha(c)) displayChar = c;
                
                // Draw hint centered above char
                if (hintChar != ' ') {
                    std::string h(1, hintChar);
                    int hWidth = display->getStringWidth(h.c_str());
                    display->drawString(currentX + (charSpacing - hWidth) / 2, currentY, h.c_str());
                }
                
                // Draw char
                std::string ch(1, displayChar);
                display->drawString(currentX, currentY + lineHeight, ch.c_str());
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
    
    display->drawString(x, y, "Menu (hold to select)");
    display->drawLine(x, y + 12, x + display->getWidth(), y + 12);
    
    const char *items[] = {"Char Picker", "Shift", "Backspace", "Send", "Back", "Stop"};
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
        if (i == 1) {
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
    
    display->drawString(x, y, "Char Picker (hold)");
    
    static const char chars[] = "0123456789.,?'!/-()&:;=+\"-_$@";
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
    display->drawString(x, y + 40, "Tap=Next Hold=Select");
}

} // namespace graphics

#endif
