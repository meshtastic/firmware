#include "modules/MorseInputModule.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputManager.h"
#include "graphics/SharedUIDisplay.h"
#include "input/ButtonThread.h"
#include <Arduino.h>

extern ButtonThread *UserButtonThread;

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

MorseInputModule::MorseInputModule() : SingleButtonInputBase("MorseInput") {}

void MorseInputModule::start(const char *header, const char *initialText, uint32_t durationMs,
                              std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header ? header : "Morse Input", initialText, durationMs, cb);
    
    currentMorse = "";
    lastInputTime = millis();
    consecutiveDots = 0;
}

void MorseInputModule::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
    // No additional action needed on press
}

void MorseInputModule::handleButtonRelease(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        SingleButtonInputBase::handleButtonRelease(now, duration);
        return;
    }
    
    // Morse input
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

void MorseInputModule::handleButtonHeld(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        if (duration > 500) {
            handleMenuSelection(menuSelection);
            ignoreRelease = true;
            waitForRelease = true;

            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            return;
        }
    } else {
        // Force update when crossing dot/dash threshold
        if (duration >= 300) {
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
        }
    }

    if (!menuOpen && duration > 2000) {
        // Open menu
        menuOpen = true;
        menuSelection = 0;
        ignoreRelease = true;
        waitForRelease = true;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void MorseInputModule::handleIdle(uint32_t now)
{
    if (!menuOpen && !currentMorse.empty()) {
        // Auto-commit character (fixed timing)
        if (now - lastInputTime > 800) {
            commitCharacter();
            consecutiveDots = 0;
        }
    } else if (!menuOpen && currentMorse.empty()) {
        // Reset backspace tracking if enough time has passed
        if (consecutiveDots > 0 && now - lastInputTime > 800) {
            consecutiveDots = 0;
        }

        // Auto-space
        if (now - lastInputTime > 2000 && !inputText.empty() && inputText.back() != ' ') {
            inputText += " ";
            lastInputTime = now;
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
        }
    }
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

void MorseInputModule::handleMenuSelection(int selection)
{
    // Let base class handle all menu items
    SingleButtonInputBase::handleMenuSelection(selection);
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
    if (!active)
        return;

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }

    drawInterface(display, x, y);
}

void MorseInputModule::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    drawMorseInterface(display, x, y);
}

void MorseInputModule::drawMorseInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    std::string activeMorse = currentMorse;
    if (buttonPressed && !menuOpen) {
        uint32_t duration = millis() - buttonPressTime;
        if (duration >= 300) {
            activeMorse += "-";
        } else {
            activeMorse += ".";
        }
    }

    // Input Text
    int lineHeightText = 13;
    int lineHeightHints = 4;
    int currentY = y;
    
    // Header
    display->drawString(x, currentY, headerText.c_str());
    currentY += lineHeightText;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 1;
    
    std::string displayInput = getDisplayTextWithCursor();
    displayInput = formatDisplayTextWithScrolling(display, displayInput);
    
    display->drawString(x, currentY, displayInput.c_str());
    
    // Horizontal Line
    currentY += lineHeightText;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 3; // Spacing

    // Character Layout
    const char *rows[] = {
        "ABCD EFGH IJKL MNOP",
        "QRST UVW XYZ ,.?!"
    };
    
    int startX = x;
    int charSpacing = 7;
    int blockSpacing = 5;
    
    for (int r = 0; r < 2; ++r) {
        const char *layout = rows[r];
        int currentX = startX;
        
        for (int i = 0; layout[i]; ++i) {
            char c = layout[i];

            if (c == ' ') {
                currentX += blockSpacing;
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
                        display->fillRect(currentX + (charSpacing - w) / 2, currentY + 2, w, h);
                    } else if (hintChar == '-') {
                        int w = 5;
                        int h = 2; // Slightly thinner dash
                        display->fillRect(currentX + (charSpacing - w) / 2, currentY, w, h);
                    }
                }
                
                // Draw char
                std::string ch(1, displayChar);
                if (isSelected) {
                    // Draw inverted (negative) for selected char
                    int w = charSpacing; // Or use display->getStringWidth(ch.c_str()) + padding
                    if (w < 6) w = 6;
                    int h = lineHeightText;
                    // Center the box horizontally
                    int boxX = currentX + (charSpacing - w) / 2;
                    
                    display->fillRect(boxX, currentY + lineHeightHints, w, h);
                    display->setColor(BLACK);
                    display->drawString(currentX, currentY + lineHeightHints, ch.c_str());
                    display->setColor(WHITE);
                } else {
                    display->drawString(currentX, currentY + lineHeightHints, ch.c_str());
                }
            }
            
            currentX += charSpacing;
        }
        currentY += lineHeightHints + lineHeightText; 
    }
}

} // namespace graphics

#endif
