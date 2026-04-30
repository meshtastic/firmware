#include "input/SingleButtonMorse.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

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
    {'+', ".-.-."}, {'-', "-....-"}, {'_', "..--.-"}, {'\"', ".-..-."}, {'$', "...-..-"}, {'@', ".--.-."},
    {'\b', "........"},
    {'\n', ".-.-."}
};

SingleButtonMorse &SingleButtonMorse::instance()
{
    static SingleButtonMorse inst;
    return inst;
}

SingleButtonMorse::SingleButtonMorse() : SingleButtonInputBase("SingleButtonMorse") {}

void SingleButtonMorse::start(const char *header, const char *initialText, uint32_t durationMs,
                              std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header ? header : "Morse Input", initialText, durationMs, cb);
    currentMorse.clear();
    lastInputTime = millis();
    consecutiveDots = 0;
}

void SingleButtonMorse::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
}

void SingleButtonMorse::handleButtonRelease(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        SingleButtonInputBase::handleButtonRelease(now, duration);
        return;
    }

    if (duration < 300) {
        consecutiveDots++;
        if (consecutiveDots == 8) {
            if (currentMorse.find('-') == std::string::npos && !inputText.empty()) {
                inputText.pop_back();
            }
            currentMorse.clear();
        } else if (consecutiveDots > 8) {
            currentMorse.clear();
        } else {
            currentMorse += ".";
        }
    } else {
        consecutiveDots = 0;
        currentMorse += "-";
    }
    lastInputTime = now;

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
}

void SingleButtonMorse::handleButtonHeld(uint32_t now, uint32_t duration)
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
    } else if (duration >= 300) {
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }

    if (!menuOpen && duration > 2000) {
        menuOpen = true;
        menuSelection = 0;
        ignoreRelease = true;
        waitForRelease = true;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void SingleButtonMorse::handleIdle(uint32_t now)
{
    if (!menuOpen && !currentMorse.empty()) {
        if (now - lastInputTime > 800) {
            commitCharacter();
            consecutiveDots = 0;
        }
    } else if (!menuOpen && currentMorse.empty()) {
        if (consecutiveDots > 0 && now - lastInputTime > 800) {
            consecutiveDots = 0;
        }

        if (now - lastInputTime > 2000 && !inputText.empty() && inputText.back() != ' ') {
            inputText += " ";
            lastInputTime = now;
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
        }
    }
}

void SingleButtonMorse::commitCharacter()
{
    char c = morseToChar(currentMorse);
    if (c == '\b') {
        if (!inputText.empty()) {
            inputText.pop_back();
        }
    } else if (c == '\n') {
        if (callback) {
            callback(inputText);
        }
        stop();
        return;
    } else if (c != 0) {
        if (shift) {
            c = toupper(c);
            if (autoShift) {
                shift = false;
            }
        } else {
            c = tolower(c);
        }
        inputText += c;

        if (c == '.' || c == '!' || c == '?') {
            shift = true;
        }
    }

    currentMorse.clear();
    lastInputTime = millis();

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
}

void SingleButtonMorse::handleMenuSelection(int selection)
{
    SingleButtonInputBase::handleMenuSelection(selection);
}

char SingleButtonMorse::morseToChar(const std::string &code)
{
    for (const auto &mc : morseTable) {
        if (code == mc.code) {
            return mc.c;
        }
    }
    return 0;
}

void SingleButtonMorse::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!active) {
        return;
    }

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }

    drawInterface(display, x, y);
}

void SingleButtonMorse::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    drawMorseInterface(display, x, y);
}

void SingleButtonMorse::drawMorseInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    std::string activeMorse = currentMorse;
    if (buttonPressed && !menuOpen) {
        uint32_t duration = millis() - buttonPressTime;
        activeMorse += (duration >= 300) ? "-" : ".";
    }

    int lineHeightText = 13;
    int lineHeightHints = 4;
    int currentY = y;

    display->drawString(x, currentY, headerText.c_str());
    currentY += lineHeightText;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 1;

    std::string displayInput = getDisplayTextWithCursor();
    displayInput = formatDisplayTextWithScrolling(display, displayInput);
    display->drawString(x, currentY, displayInput.c_str());

    currentY += lineHeightText;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 3;

    const char *rows[] = {"ABCD EFGH IJKL MNOP", "QRST UVW XYZ ,.?!"};

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

            std::string code;
            for (const auto &mc : morseTable) {
                if (mc.c == c) {
                    code = mc.code;
                    break;
                }
            }

            bool isVisible = false;
            char hintChar = ' ';
            bool isSelected = false;

            if (code.find(activeMorse) == 0) {
                isVisible = true;
                if (code == activeMorse) {
                    isSelected = true;
                } else {
                    hintChar = code[activeMorse.length()];
                }
            }

            if (isVisible) {
                char displayChar = shift ? c : tolower(c);
                if (!isalpha(c)) {
                    displayChar = c;
                }

                if (!isSelected && hintChar != ' ') {
                    if (hintChar == '.') {
                        display->fillRect(currentX + 2, currentY + 2, 3, 3);
                    } else if (hintChar == '-') {
                        display->fillRect(currentX + 1, currentY, 5, 2);
                    }
                }

                std::string ch(1, displayChar);
                if (isSelected) {
                    int width = charSpacing < 6 ? 6 : charSpacing;
                    int boxX = currentX + (charSpacing - width) / 2;
                    display->fillRect(boxX, currentY + lineHeightHints, width, lineHeightText);
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