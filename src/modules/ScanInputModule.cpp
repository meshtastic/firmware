#include "modules/ScanInputModule.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputManager.h"
#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

namespace graphics
{

ScanInputModule &ScanInputModule::instance()
{
    static ScanInputModule inst;
    return inst;
}

ScanInputModule::ScanInputModule() : SingleButtonInputBase("ScanInput") {}

void ScanInputModule::start(const char *header, const char *initialText, uint32_t durationMs,
                            std::function<void(const std::string &)> cb)
{
    SingleButtonInputBase::start(header, initialText, durationMs, cb);
    
    // Reset scan state
    resetToGroupLevel();
    
    // Initialize scan timing
    nextScanTime = millis() + SCAN_INTERVAL_MS;
}

void ScanInputModule::handleButtonPress(uint32_t now)
{
    SingleButtonInputBase::handleButtonPress(now);
    // No special action needed on press for scan input
}

void ScanInputModule::handleButtonRelease(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        SingleButtonInputBase::handleButtonRelease(now, duration);
        return;
    }
    
    // Short press (<1s) - Select current item
    if (duration < 1000) {
        selectCurrentItem();
        
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void ScanInputModule::handleButtonHeld(uint32_t now, uint32_t duration)
{
    // Long press (≥2s) opens menu
    SingleButtonInputBase::handleButtonHeld(now, duration);
}

void ScanInputModule::handleIdle(uint32_t now)
{
    if (menuOpen) {
        return;
    }
    
    // Auto-advance scan
    if (now >= nextScanTime) {
        advanceScan();
        
        // Check for timing drift and resync if needed (>1.6 seconds off)
        uint32_t drift = (now - nextScanTime);
        if (drift > SCAN_INTERVAL_MS * 2) {
            // Major drift detected, resync
            nextScanTime = now + SCAN_INTERVAL_MS;
        } else {
            // Maintain consistent interval
            nextScanTime += SCAN_INTERVAL_MS;
        }
        
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

ScanInputModule::SubgroupInfo ScanInputModule::getSubgroupInfo(int subgroupIndex) const
{
    // Each group has 10 characters split as [3, 3, 4]
    switch (subgroupIndex) {
    case 0:
        return {0, 3};
    case 1:
        return {3, 3};
    case 2:
        return {6, 4};
    default:
        return {0, 0};
    }
}

int ScanInputModule::getAbsoluteCharIndex() const
{
    int groupStart = currentGroup * CHARS_PER_GROUP;
    SubgroupInfo info = getSubgroupInfo(currentSubgroup);
    return groupStart + info.startIndex + currentCharIndex;
}

void ScanInputModule::advanceScan()
{
    switch (currentLevel) {
    case LEVEL_GROUP:
        currentGroup = (currentGroup + 1) % GROUPS;
        break;
        
    case LEVEL_SUBGROUP:
        currentSubgroup = (currentSubgroup + 1) % 3;
        break;
        
    case LEVEL_CHARACTER: {
        SubgroupInfo info = getSubgroupInfo(currentSubgroup);
        currentCharIndex = (currentCharIndex + 1) % info.count;
        break;
    }
    }
}

void ScanInputModule::selectCurrentItem()
{
    switch (currentLevel) {
    case LEVEL_GROUP:
        // Drill down to subgroup level
        currentLevel = LEVEL_SUBGROUP;
        currentSubgroup = 0;
        break;
        
    case LEVEL_SUBGROUP:
        // Drill down to character level
        currentLevel = LEVEL_CHARACTER;
        currentCharIndex = 0;
        break;
        
    case LEVEL_CHARACTER: {
        // Add character to input text
        int absIndex = getAbsoluteCharIndex();
        if (absIndex >= 0 && absIndex < (int)strlen(CHARACTERS)) {
            char c = CHARACTERS[absIndex];
            
            // Apply shift
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
        // Return to group level
        resetToGroupLevel();
        break;
    }
    }
}

void ScanInputModule::resetToGroupLevel()
{
    currentLevel = LEVEL_GROUP;
    currentGroup = 0;
    currentSubgroup = 0;
    currentCharIndex = 0;
}

void ScanInputModule::handleModeSwitch(int modeIndex)
{
    if (modeIndex == 0) { // Switch to Morse mode
        // Save current text and callback
        std::string savedText = inputText;
        auto savedCallback = callback;
        std::string savedHeader = headerText;
        
        // Stop this module without calling callback
        stop(false);
        
        // Switch mode
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_MORSE);
        
        // Start morse module with saved state
        SingleButtonInputManager::instance().start(savedHeader.c_str(), savedText.c_str(), 0, savedCallback);
    } else if (modeIndex == 2) { // Switch to Special Characters mode
        // Save current text and callback
        std::string savedText = inputText;
        auto savedCallback = callback;
        std::string savedHeader = headerText;
        
        // Stop this module without calling callback
        stop(false);
        
        // Switch mode
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_SPECIAL_CHARACTERS);
        
        // Start special character module with saved state
        SingleButtonInputManager::instance().start(savedHeader.c_str(), savedText.c_str(), 0, savedCallback);
    } else if (modeIndex == 3) { // Switch to Grid Keyboard mode
        // Save current text and callback
        std::string savedText = inputText;
        auto savedCallback = callback;
        std::string savedHeader = headerText;
        
        // Stop this module without calling callback
        stop(false);
        
        // Switch mode
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_GRID_KEYBOARD);
        
        // Start grid keyboard module with saved state
        SingleButtonInputManager::instance().start(savedHeader.c_str(), savedText.c_str(), 0, savedCallback);
    } else {
        // Already in Scan mode, just close menu
        menuOpen = false;
        inputModeMenuOpen = false;
    }
}

void ScanInputModule::handleMenuSelection(int selection)
{
    // Let base class handle all menu items
    SingleButtonInputBase::handleMenuSelection(selection);
}

void ScanInputModule::drawInterface(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    int lineHeight = 10;
    int currentY = y;

    // Header
    display->drawString(x, currentY, headerText.c_str());
    currentY += lineHeight + 2;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 2;

    // Input Text with blinking cursor
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
    currentY += 3;
    display->drawLine(x, currentY, x + display->getWidth(), currentY);
    currentY += 2;

    // Character grid (20 characters per row, 2 rows)
    const char *rows[] = {
        "ABCDEFGHIJKLMNOPQRST",
        "UVWXYZ,.?0123456789_"
    };

    int charSpacing = 6;
    int charWidth = 6;

    for (int r = 0; r < 2; ++r) {
        const char *layout = rows[r];
        int currentX = x + 2; // Small left margin

        for (int i = 0; layout[i]; ++i) {
            char c = layout[i];
            int charAbsIndex = (r == 0) ? i : (20 + i);

            // Determine if this character should be highlighted
            bool isHighlighted = false;

            if (currentLevel == LEVEL_GROUP) {
                // Highlight entire group
                int groupStart = currentGroup * CHARS_PER_GROUP;
                int groupEnd = groupStart + CHARS_PER_GROUP;
                if (charAbsIndex >= groupStart && charAbsIndex < groupEnd) {
                    isHighlighted = true;
                }
            } else if (currentLevel == LEVEL_SUBGROUP) {
                // Highlight subgroup
                SubgroupInfo info = getSubgroupInfo(currentSubgroup);
                int subgroupStart = currentGroup * CHARS_PER_GROUP + info.startIndex;
                int subgroupEnd = subgroupStart + info.count;
                if (charAbsIndex >= subgroupStart && charAbsIndex < subgroupEnd) {
                    isHighlighted = true;
                }
            } else if (currentLevel == LEVEL_CHARACTER) {
                // Highlight single character
                int highlightIndex = getAbsoluteCharIndex();
                if (charAbsIndex == highlightIndex) {
                    isHighlighted = true;
                }
            }

            // Draw character (respect shift state for display)
            char displayChar = shift ? c : tolower(c);
            // Numbers and punctuation don't have lowercase variants
            if (!isalpha(c)) displayChar = c;
            
            if (isHighlighted) {
                // Inverted (white background, black text)
                display->fillRect(currentX, currentY, charWidth, lineHeight);
                display->setColor(BLACK);
                char str[2] = {displayChar, '\0'};
                display->drawString(currentX, currentY, str);
                display->setColor(WHITE);
            } else {
                // Normal
                char str[2] = {displayChar, '\0'};
                display->drawString(currentX, currentY, str);
            }

            currentX += charSpacing;
        }

        currentY += lineHeight;
    }
}

void ScanInputModule::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!active)
        return;

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }

    drawInterface(display, x, y);
}

} // namespace graphics

#endif
